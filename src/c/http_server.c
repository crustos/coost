#include "co/c/http_server.h"
#include "co/c/sock.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct co_http_conn {
    int fd;
    co_http_msg m;
    co_buf out;
    size_t out_off;
    /* the timeout runs from here: accepted, last response queued, first
     * byte of a new request, or last write progress */
    int64_t mark;
    int eof;        /* the peer has closed its side */
    int closing;    /* close once out is written */
    int dead;
    int responded;
    int head_req;
    co_http_server* s;
};

struct co_http_server {
    int lfd;
    void (*on_req)(co_http_conn* c, co_http_msg* m, void* ud);
    void* ud;
    int timeout;
    size_t max_body;
    int max_conns;
    int stopping;
    co_http_conn** conns;
    int n;
    int cap;
    struct pollfd* pfd;
    int pcap;
    long accepted;
};

static int64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

co_http_server* co_http_server_new(int lfd,
                                   void (*on_req)(co_http_conn* c, co_http_msg* m, void* ud),
                                   void* ud) {
    co_http_server* s = (co_http_server*)calloc(1, sizeof(*s));
    if (s == 0) return 0;
    s->lfd = lfd;
    s->on_req = on_req;
    s->ud = ud;
    s->timeout = 5000;
    s->max_body = 8 << 20;
    s->max_conns = 1024;
    return s;
}

static void conn_free(co_http_conn* c) {
    co_sock_close(c->fd);
    co_http_msg_free(&c->m);
    co_buf_free(&c->out);
    free(c);
}

void co_http_server_free(co_http_server* s) {
    int i;
    if (s == 0) return;
    for (i = 0; i < s->n; ++i) conn_free(s->conns[i]);
    free(s->conns);
    free(s->pfd);
    free(s);
}

void co_http_server_set_limits(co_http_server* s, int timeout_ms, size_t max_body, int max_conns) {
    s->timeout = timeout_ms > 0 ? timeout_ms : 1;
    s->max_body = max_body;
    s->max_conns = max_conns > 0 ? max_conns : 1;
}

void co_http_server_stop(co_http_server* s) { s->stopping = 1; }
int co_http_server_done(const co_http_server* s) { return s->stopping && s->n == 0; }
int co_http_server_conns(const co_http_server* s) { return s->n; }
long co_http_server_accepted(const co_http_server* s) { return s->accepted; }

static int pending(const co_http_conn* c) { return c->out_off < c->out.len; }

/* write what the socket takes; 0 ok, -1 error */
static int flush(co_http_conn* c, int64_t now) {
    while (pending(c)) {
        const int w = co_sock_send_some(c->fd, c->out.p + c->out_off, c->out.len - c->out_off);
        if (w < 0) return -1;
        if (w == 0) break;
        c->out_off += (size_t)w;
        c->mark = now;
    }
    if (!pending(c)) {
        c->out_off = 0;
        if (c->out.cap > (1u << 20)) co_buf_free(&c->out); /* don't hold on to a big reply */
        else co_buf_clear(&c->out);
        if (c->closing) c->dead = 1;
    }
    return 0;
}

void co_http_server_respond(co_http_conn* c, int status, const char* hdrs, size_t hlen,
                            const void* body, size_t blen) {
    if (c->responded) return;
    c->responded = 1;
    const int keep = c->m.keepalive && !c->s->stopping;
    if (co_http_build_response(&c->out, status, hdrs, hlen, body, blen, keep, c->head_req) != 0) {
        c->dead = 1;
        return;
    }
    if (!keep) c->closing = 1;
}

/* answer every complete request buffered on @c, writing as we go; stops at
 * the first response the socket won't take whole, so responses stay in
 * order and a client that doesn't read can't make us buffer without end */
static int process(co_http_server* s, co_http_conn* c, int64_t now) {
    int handled = 0;
    for (;;) {
        if (pending(c) && flush(c, now) != 0) c->dead = 1;
        if (c->dead || c->closing || pending(c)) break;
        const int r = co_http_parse(&c->m, 1, s->max_body, 0, c->eof);
        if (r == CO_HTTP_MORE) break;
        if (r == CO_HTTP_CLOSED) {
            c->dead = 1;
            break;
        }
        if (r != CO_HTTP_OK) {
            const int code = co_http_error_status(r);
            const char* reason = co_http_reason(code);
            co_http_build_response(&c->out, code, 0, 0, reason, strlen(reason), 0, 0);
            c->closing = 1;
            continue; /* flush it */
        }
        c->responded = 0;
        c->head_req = c->m.h.method_len == 4 && memcmp(c->m.head.p, "HEAD", 4) == 0;
        s->on_req(c, &c->m, s->ud);
        if (!c->responded) {
            const char* msg = "handler did not respond";
            co_http_server_respond(c, 500, 0, 0, msg, strlen(msg));
        }
        c->mark = now;
        handled++;
    }
    return handled;
}

static void read_some(co_http_conn* c, int64_t now) {
    co_buf* in = &c->m.in;
    const int was_busy = co_http_msg_busy(&c->m);
    if (co_buf_reserve(in, in->len + 65536) != 0) {
        c->dead = 1;
        return;
    }
    const int r = co_sock_recv(c->fd, in->p + in->len, (int)(in->cap - in->len - 1), 0);
    if (r > 0) {
        in->len += (size_t)r;
        in->p[in->len] = 0;
        if (!was_busy) c->mark = now; /* a new request: its own deadline */
    } else if (r == 0) {
        c->eof = 1;
    } else if (errno != ETIMEDOUT) { /* ETIMEDOUT: nothing there after all */
        c->dead = 1;
    }
}

static void accept_some(co_http_server* s, int64_t now) {
    int i;
    for (i = 0; i < 64 && s->n < s->max_conns; ++i) {
        const int fd = co_sock_accept(s->lfd, 0);
        if (fd < 0) break;
        if (s->n == s->cap) {
            const int cap = s->cap ? s->cap * 2 : 16;
            co_http_conn** p = (co_http_conn**)realloc(s->conns, sizeof(*p) * (size_t)cap);
            if (p == 0) {
                co_sock_close(fd);
                break;
            }
            s->conns = p;
            s->cap = cap;
        }
        co_http_conn* c = (co_http_conn*)calloc(1, sizeof(*c));
        if (c == 0) {
            co_sock_close(fd);
            break;
        }
        co_sock_set_nodelay(fd, 1);
        c->fd = fd;
        c->s = s;
        c->mark = now;
        s->conns[s->n++] = c;
        s->accepted++;
    }
}

static void reap(co_http_server* s) {
    int i, j = 0;
    for (i = 0; i < s->n; ++i) {
        if (s->conns[i]->dead) conn_free(s->conns[i]);
        else s->conns[j++] = s->conns[i];
    }
    s->n = j;
}

int co_http_server_step(co_http_server* s, int ms) {
    int64_t now = mono_ms();
    int i, np = 0, handled = 0;
    if (s->stopping) { /* nothing more is read; finish what is being written */
        for (i = 0; i < s->n; ++i) {
            if (!pending(s->conns[i])) s->conns[i]->dead = 1;
        }
    }
    reap(s);
    if (s->pcap < s->n + 1) {
        const int cap = s->n + 16;
        struct pollfd* p = (struct pollfd*)realloc(s->pfd, sizeof(*p) * (size_t)cap);
        if (p == 0) return -1;
        s->pfd = p;
        s->pcap = cap;
    }
    const int listening = !s->stopping && s->n < s->max_conns;
    if (listening) {
        s->pfd[np].fd = s->lfd;
        s->pfd[np].events = POLLIN;
        np++;
    }
    int wait = ms;
    for (i = 0; i < s->n; ++i) {
        co_http_conn* c = s->conns[i];
        s->pfd[np].fd = c->fd;
        s->pfd[np].events = pending(c) ? POLLOUT : POLLIN;
        np++;
        int64_t left = c->mark + s->timeout - now;
        if (left < 0) left = 0;
        if (wait < 0 || left < wait) wait = (int)left;
    }
    const int polled = s->n;
    const int r = poll(s->pfd, (nfds_t)np, wait);
    if (r < 0) return errno == EINTR ? 0 : -1;
    now = mono_ms();

    int k = 0;
    if (listening) {
        if (s->pfd[0].revents & POLLIN) accept_some(s, now);
        k = 1;
    }
    for (i = 0; i < polled; ++i, ++k) {
        co_http_conn* c = s->conns[i];
        const short rev = s->pfd[k].revents;
        if (rev & POLLNVAL) {
            c->dead = 1;
        } else if (pending(c)) {
            if (rev & (POLLOUT | POLLERR | POLLHUP)) handled += process(s, c, now);
        } else if (rev & (POLLIN | POLLERR | POLLHUP)) {
            read_some(c, now);
            if (!c->dead) handled += process(s, c, now);
        }
        if (!c->dead && now - c->mark > s->timeout) c->dead = 1;
    }
    reap(s);
    return handled;
}
