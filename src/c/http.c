#include "co/c/http.h"
#include "co/c/sock.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---- co_buf ------------------------------------------------------------ */

int co_buf_reserve(co_buf* b, size_t n) {
    if (b->cap >= n + 1) return 0;
    size_t c = b->cap ? b->cap : 256;
    while (c < n + 1) c *= 2;
    char* p = (char*)realloc(b->p, c);
    if (p == 0) {
        errno = ENOMEM;
        return -1;
    }
    b->p = p;
    b->cap = c;
    b->p[b->len] = 0;
    return 0;
}

int co_buf_append(co_buf* b, const void* s, size_t n) {
    if (co_buf_reserve(b, b->len + n) != 0) return -1;
    if (n) memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
    return 0;
}

static int buf_str(co_buf* b, const char* s) { return co_buf_append(b, s, strlen(s)); }

void co_buf_consume(co_buf* b, size_t n) {
    if (n >= b->len) {
        b->len = 0;
    } else {
        memmove(b->p, b->p + n, b->len - n);
        b->len -= n;
    }
    if (b->p) b->p[b->len] = 0;
}

void co_buf_clear(co_buf* b) {
    b->len = 0;
    if (b->p) b->p[0] = 0;
}

void co_buf_free(co_buf* b) {
    free(b->p);
    b->p = 0;
    b->len = b->cap = 0;
}

/* ---- head parsing ------------------------------------------------------ */

static int is_tchar(unsigned char c) {
    return c != 0 && (isalnum(c) || strchr("!#$%&'*+-.^_`|~", c) != 0);
}

static int is_ws(char c) { return c == ' ' || c == '\t'; }

/* end of the line starting at @pos (excluding CR), and *next past its LF.
 * The head is known to end in a blank line, so an LF is always found. */
static size_t line_end(const char* s, size_t pos, size_t len, size_t* next) {
    const char* lf = (const char*)memchr(s + pos, '\n', len - pos);
    size_t e = lf ? (size_t)(lf - s) : len;
    *next = e + 1;
    if (e > pos && s[e - 1] == '\r') e--;
    return e;
}

size_t co_http_head_end(const char* s, size_t len, size_t from) {
    size_t i = from > 3 ? from - 3 : 0;
    for (; i < len; ++i) {
        if (s[i] != '\n') continue;
        if (i + 1 < len && s[i + 1] == '\n') return i + 2;
        if (i + 2 < len && s[i + 1] == '\r' && s[i + 2] == '\n') return i + 3;
    }
    return 0;
}

static int check_fields(const char* s, size_t pos, size_t len) {
    int count = 0;
    for (;;) {
        size_t next, i, j;
        const size_t e = line_end(s, pos, len, &next);
        if (e == pos) return 0; /* the blank line */
        if (++count > 256) return -1;
        if (is_ws(s[pos])) return -1; /* obsolete line folding */
        for (i = pos; i < e && is_tchar((unsigned char)s[i]); ++i) {}
        if (i == pos || i >= e || s[i] != ':') return -1;
        for (j = i + 1; j < e; ++j) {
            const unsigned char c = (unsigned char)s[j];
            if ((c < 0x20 && c != '\t') || c == 0x7f) return -1;
        }
        pos = next;
    }
}

static int parse_version(const char* s, size_t n, int* minor) {
    if (n != 8 || memcmp(s, "HTTP/1.", 7) != 0 || s[7] < '0' || s[7] > '9') return -1;
    *minor = s[7] == '0' ? 0 : 1;
    return 0;
}

int co_http_parse_request_head(const char* s, size_t len, co_http_head* h) {
    size_t next, i = 0, t;
    memset(h, 0, sizeof(*h));
    const size_t e = line_end(s, 0, len, &next);
    while (i < e && is_tchar((unsigned char)s[i])) i++;
    if (i == 0 || i >= e || s[i] != ' ') return -1;
    h->method_len = i++;
    for (t = i; i < e && (unsigned char)s[i] > 0x20 && s[i] != 0x7f; ++i) {}
    if (i == t || i >= e || s[i] != ' ') return -1;
    h->target_off = t;
    h->target_len = i - t;
    if (parse_version(s + i + 1, e - i - 1, &h->minor) != 0) return -1;
    h->fields_off = next;
    h->len = len;
    return check_fields(s, next, len);
}

int co_http_parse_response_head(const char* s, size_t len, co_http_head* h) {
    size_t next, i;
    memset(h, 0, sizeof(*h));
    const size_t e = line_end(s, 0, len, &next);
    if (e < 12 || parse_version(s, 8, &h->minor) != 0 || s[8] != ' ') return -1;
    for (i = 9; i < 12; ++i) {
        if (s[i] < '0' || s[i] > '9') return -1;
        h->status = h->status * 10 + (s[i] - '0');
    }
    if (h->status < 100) return -1;
    if (e > 12 && s[12] != ' ') return -1;
    h->reason_off = e > 12 ? 13 : 12;
    h->reason_len = e - h->reason_off;
    for (i = h->reason_off; i < e; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) return -1;
    }
    h->fields_off = next;
    h->len = len;
    return check_fields(s, next, len);
}

int co_http_header_next(const char* s, const co_http_head* h, size_t* pos,
                        const char** name, size_t* nlen,
                        const char** value, size_t* vlen) {
    size_t p = *pos ? *pos : h->fields_off, next, c, v;
    if (p >= h->len) return 0;
    size_t e = line_end(s, p, h->len, &next);
    if (e == p) return 0;
    c = (size_t)((const char*)memchr(s + p, ':', e - p) - s);
    for (v = c + 1; v < e && is_ws(s[v]); ++v) {}
    while (e > v && is_ws(s[e - 1])) e--;
    *name = s + p;
    *nlen = c - p;
    *value = s + v;
    *vlen = e - v;
    *pos = next;
    return 1;
}

static int name_is(const char* n, size_t nlen, const char* name) {
    return strlen(name) == nlen && strncasecmp(n, name, nlen) == 0;
}

const char* co_http_header_get(const char* s, const co_http_head* h,
                               const char* name, size_t* vlen) {
    size_t pos = 0, nl;
    const char *n, *v;
    while (co_http_header_next(s, h, &pos, &n, &nl, &v, vlen)) {
        if (name_is(n, nl, name)) return v;
    }
    return 0;
}

int co_http_header_has_token(const char* s, const co_http_head* h,
                             const char* name, const char* token) {
    size_t pos = 0, nl, vl;
    const char *n, *v;
    const size_t tl = strlen(token);
    while (co_http_header_next(s, h, &pos, &n, &nl, &v, &vl)) {
        if (!name_is(n, nl, name)) continue;
        size_t i = 0;
        while (i < vl) {
            size_t a = i, b;
            while (i < vl && v[i] != ',') i++;
            b = i++;
            while (a < b && is_ws(v[a])) a++;
            while (b > a && is_ws(v[b - 1])) b--;
            if (b - a == tl && strncasecmp(v + a, token, tl) == 0) return 1;
        }
    }
    return 0;
}

int64_t co_http_content_length(const char* s, const co_http_head* h) {
    size_t pos = 0, nl, vl, i;
    const char *n, *v;
    int64_t r = -1;
    while (co_http_header_next(s, h, &pos, &n, &nl, &v, &vl)) {
        if (!name_is(n, nl, "content-length")) continue;
        int64_t x = 0;
        if (vl == 0 || vl > 18) return -2;
        for (i = 0; i < vl; ++i) {
            if (v[i] < '0' || v[i] > '9') return -2;
            x = x * 10 + (v[i] - '0');
        }
        if (r >= 0 && r != x) return -2;
        r = x;
    }
    return r;
}

int co_http_field_ok(const char* name, const char* value) {
    const char* p;
    if (name == 0 || *name == 0 || value == 0) return 0;
    for (p = name; *p; ++p) {
        if (!is_tchar((unsigned char)*p)) return 0;
    }
    for (p = value; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if ((c < 0x20 && c != '\t') || c == 0x7f) return 0;
    }
    return 1;
}

/* ---- chunked ----------------------------------------------------------- */

enum {
    CH_SIZE, CH_EXT, CH_DATA, CH_DATA_CR, CH_DATA_LF,
    CH_TRAILER_START, CH_TRAILER, CH_END_LF, CH_DONE
};

#define CH_MAX_SKIP 16384

void co_http_chunked_init(co_http_chunked* d) { memset(d, 0, sizeof(*d)); }

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

long co_http_chunked_decode(co_http_chunked* d, char* buf, size_t* len) {
    size_t src = 0, dst = 0;
    const size_t n = *len;
    while (src < n) {
        const char c = buf[src];
        switch (d->state) {
          case CH_SIZE: {
            const int x = hexval(c);
            if (x >= 0) {
                if (++d->digits > 15) return -1;
                d->left = d->left * 16 + (size_t)x;
                src++;
                break;
            }
            if (d->digits == 0) return -1;
            if (c == '\n') {
                src++;
                d->digits = 0;
                d->state = d->left ? CH_DATA : CH_TRAILER_START;
            } else if (c == ';' || c == '\r' || is_ws(c)) {
                d->state = CH_EXT;
            } else {
                return -1;
            }
            break;
          }
          case CH_EXT:
            src++;
            if (c == '\n') {
                d->digits = 0;
                d->state = d->left ? CH_DATA : CH_TRAILER_START;
            } else if (++d->skipped > CH_MAX_SKIP) {
                return -1;
            }
            break;
          case CH_DATA: {
            size_t k = n - src;
            if (k > d->left) k = d->left;
            memmove(buf + dst, buf + src, k);
            dst += k;
            src += k;
            d->left -= k;
            if (d->left == 0) d->state = CH_DATA_CR;
            break;
          }
          case CH_DATA_CR:
            src++;
            if (c == '\r') d->state = CH_DATA_LF;
            else if (c == '\n') d->state = CH_SIZE;
            else return -1;
            break;
          case CH_DATA_LF:
            src++;
            if (c != '\n') return -1;
            d->state = CH_SIZE;
            break;
          case CH_TRAILER_START:
            if (c == '\r') {
                src++;
                d->state = CH_END_LF;
            } else if (c == '\n') {
                src++;
                d->state = CH_DONE;
            } else {
                d->state = CH_TRAILER;
            }
            break;
          case CH_TRAILER:
            src++;
            if (c == '\n') d->state = CH_TRAILER_START;
            else if (++d->skipped > CH_MAX_SKIP) return -1;
            break;
          case CH_END_LF:
            src++;
            if (c != '\n') return -1;
            d->state = CH_DONE;
            break;
          default:
            break;
        }
        if (d->state == CH_DONE) {
            const size_t rest = n - src;
            memmove(buf + dst, buf + src, rest);
            *len = dst;
            return (long)rest;
        }
    }
    *len = dst;
    return -2;
}

/* ---- messages ---------------------------------------------------------- */

enum { PH_HEAD, PH_LEN, PH_CHUNKED, PH_CLOSE, PH_DONE };

void co_http_msg_init(co_http_msg* m) { memset(m, 0, sizeof(*m)); }

void co_http_msg_reset(co_http_msg* m) {
    co_buf_clear(&m->in);
    co_buf_clear(&m->head);
    co_buf_clear(&m->body);
    memset(&m->h, 0, sizeof(m->h));
    m->keepalive = 0;
    m->phase = PH_HEAD;
    m->scanned = 0;
    m->left = 0;
}

void co_http_msg_free(co_http_msg* m) {
    co_buf_free(&m->in);
    co_buf_free(&m->head);
    co_buf_free(&m->body);
    memset(m, 0, sizeof(*m));
}

const char* co_http_msg_header(const co_http_msg* m, const char* name, size_t* vlen) {
    if (m->head.len == 0) return 0;
    return co_http_header_get(m->head.p, &m->h, name, vlen);
}

int co_http_msg_busy(const co_http_msg* m) {
    return m->in.len > 0 || (m->phase != PH_HEAD && m->phase != PH_DONE);
}

static int wants_keepalive(const co_http_msg* m) {
    const char* s = m->head.p;
    if (m->h.minor >= 1) return !co_http_header_has_token(s, &m->h, "connection", "close");
    return co_http_header_has_token(s, &m->h, "connection", "keep-alive");
}

/* 1 if the last coding in Transfer-Encoding is chunked */
static int te_chunked(const char* te, size_t tl) {
    while (tl > 0 && is_ws(te[tl - 1])) tl--;
    return tl >= 7 && strncasecmp(te + tl - 7, "chunked", 7) == 0 &&
           (tl == 7 || te[tl - 8] == ',' || is_ws(te[tl - 8]));
}

static int body_len(co_http_msg* m, int64_t cl, size_t max_body) {
    if ((uint64_t)cl > max_body) return CO_HTTP_BODY_TOO_LARGE;
    m->left = (size_t)cl;
    m->phase = cl > 0 ? PH_LEN : PH_DONE;
    if (cl > 0 && co_buf_reserve(&m->body, (size_t)cl) != 0) return CO_HTTP_ERR;
    return CO_HTTP_OK;
}

static int begin_request_body(co_http_msg* m, size_t max_body) {
    size_t tl = 0;
    const char* te = co_http_header_get(m->head.p, &m->h, "transfer-encoding", &tl);
    const int64_t cl = co_http_content_length(m->head.p, &m->h);
    if (te) {
        /* a request carrying both is a smuggling vector, and plain chunked
         * is the only coding understood */
        if (cl != -1 || tl != 7 || strncasecmp(te, "chunked", 7) != 0) return CO_HTTP_BAD;
        co_http_chunked_init(&m->ch);
        m->phase = PH_CHUNKED;
        return CO_HTTP_OK;
    }
    if (cl == -2) return CO_HTTP_BAD;
    return body_len(m, cl < 0 ? 0 : cl, max_body);
}

static int begin_response_body(co_http_msg* m, size_t max_body, int head_request) {
    size_t tl = 0;
    const int st = m->h.status;
    if (head_request || st < 200 || st == 204 || st == 304) {
        m->phase = PH_DONE;
        return CO_HTTP_OK;
    }
    const char* te = co_http_header_get(m->head.p, &m->h, "transfer-encoding", &tl);
    if (te) {
        if (te_chunked(te, tl)) {
            co_http_chunked_init(&m->ch);
            m->phase = PH_CHUNKED;
        } else {
            m->keepalive = 0;
            m->phase = PH_CLOSE;
        }
        return CO_HTTP_OK;
    }
    const int64_t cl = co_http_content_length(m->head.p, &m->h);
    if (cl == -2) return CO_HTTP_BAD;
    if (cl == -1) {
        m->keepalive = 0;
        m->phase = PH_CLOSE;
        return CO_HTTP_OK;
    }
    return body_len(m, cl, max_body);
}

static int parse_step(co_http_msg* m, int is_req, size_t max_body, int head_request, int eof) {
    co_buf* b = &m->body;
    if (m->phase == PH_DONE) {
        m->phase = PH_HEAD;
        m->scanned = 0;
    }
    for (;;) {
        switch (m->phase) {
          case PH_HEAD: {
            if (is_req && m->scanned == 0) { /* stray CRLFs between requests */
                size_t k = 0;
                while (k < m->in.len && (m->in.p[k] == '\r' || m->in.p[k] == '\n')) k++;
                if (k) co_buf_consume(&m->in, k);
            }
            const size_t end = m->in.len ? co_http_head_end(m->in.p, m->in.len, m->scanned) : 0;
            if (end == 0) {
                if (m->in.len >= CO_HTTP_MAX_HEAD) return CO_HTTP_HEAD_TOO_LARGE;
                m->scanned = m->in.len;
                if (eof) return m->in.len == 0 ? CO_HTTP_CLOSED : CO_HTTP_BAD;
                return CO_HTTP_MORE;
            }
            if (end > CO_HTTP_MAX_HEAD) return CO_HTTP_HEAD_TOO_LARGE;
            m->scanned = 0;
            co_buf_clear(&m->head);
            co_buf_clear(b);
            if (co_buf_append(&m->head, m->in.p, end) != 0) return CO_HTTP_ERR;
            co_buf_consume(&m->in, end);
            const int bad = is_req ? co_http_parse_request_head(m->head.p, end, &m->h)
                                   : co_http_parse_response_head(m->head.p, end, &m->h);
            if (bad) return CO_HTTP_BAD;
            if (!is_req && m->h.status < 200 && m->h.status != 101) continue; /* interim */
            m->keepalive = wants_keepalive(m);
            const int r = is_req ? begin_request_body(m, max_body)
                                 : begin_response_body(m, max_body, head_request);
            if (r != CO_HTTP_OK) return r;
            break;
          }
          case PH_LEN: {
            const size_t k = m->in.len < m->left ? m->in.len : m->left;
            if (co_buf_append(b, m->in.p, k) != 0) return CO_HTTP_ERR;
            co_buf_consume(&m->in, k);
            m->left -= k;
            if (m->left == 0) {
                m->phase = PH_DONE;
                break;
            }
            return eof ? CO_HTTP_BAD : CO_HTTP_MORE;
          }
          case PH_CHUNKED: {
            const size_t decoded = b->len;
            if (co_buf_append(b, m->in.p, m->in.len) != 0) return CO_HTTP_ERR;
            co_buf_clear(&m->in);
            size_t n = b->len - decoded;
            const long r = co_http_chunked_decode(&m->ch, b->p + decoded, &n);
            if (r == -1) return CO_HTTP_BAD;
            b->len = decoded + n;
            if (r >= 0) { /* bytes after the body belong to the next message */
                if (co_buf_append(&m->in, b->p + b->len, (size_t)r) != 0) return CO_HTTP_ERR;
                b->p[b->len] = 0;
                m->phase = PH_DONE;
                break;
            }
            b->p[b->len] = 0;
            if (b->len > max_body) return CO_HTTP_BODY_TOO_LARGE;
            return eof ? CO_HTTP_BAD : CO_HTTP_MORE;
          }
          case PH_CLOSE:
            if (co_buf_append(b, m->in.p, m->in.len) != 0) return CO_HTTP_ERR;
            co_buf_clear(&m->in);
            if (b->len > max_body) return CO_HTTP_BODY_TOO_LARGE;
            if (!eof) return CO_HTTP_MORE;
            m->phase = PH_DONE;
            break;
          default: /* PH_DONE */
            if (b->p) b->p[b->len] = 0;
            return CO_HTTP_OK;
        }
    }
}

int co_http_parse(co_http_msg* m, int is_req, size_t max_body, int head_request, int eof) {
    const int r = parse_step(m, is_req, max_body, head_request, eof);
    if (r != CO_HTTP_OK && r != CO_HTTP_MORE) {
        m->keepalive = 0;
        m->phase = PH_HEAD; /* unusable; the caller closes */
    }
    return r;
}

/* append what the socket has to @b; >0 bytes, 0 closed, -1 error */
static int fill(int fd, co_buf* b, int ms) {
    if (co_buf_reserve(b, b->len + 16384) != 0) return -1;
    const int r = co_sock_recv(fd, b->p + b->len, (int)(b->cap - b->len - 1), ms);
    if (r > 0) {
        b->len += (size_t)r;
        b->p[b->len] = 0;
    }
    return r;
}

static int read_msg(int fd, co_http_msg* m, int ms, size_t max_body, int is_req, int head_request) {
    int eof = 0;
    for (;;) {
        const int r = co_http_parse(m, is_req, max_body, head_request, eof);
        if (r != CO_HTTP_MORE) return r;
        const int k = fill(fd, &m->in, ms);
        if (k < 0) {
            m->keepalive = 0;
            return CO_HTTP_ERR;
        }
        if (k == 0) eof = 1;
    }
}

int co_http_read_request(int fd, co_http_msg* m, int ms, size_t max_body) {
    return read_msg(fd, m, ms, max_body, 1, 0);
}

int co_http_read_response(int fd, co_http_msg* m, int ms, size_t max_body, int head_request) {
    return read_msg(fd, m, ms, max_body, 0, head_request);
}

static int send_buf(int fd, co_buf* b, const void* body, size_t blen, int ms) {
    int r = 0;
    /* small bodies go out with the head in one write */
    if (blen > 0 && blen <= 65536) {
        if (co_buf_append(b, body, blen) != 0) r = -1;
        blen = 0;
    }
    if (r == 0 && co_sock_send(fd, b->p, (int)b->len, ms) < 0) r = -1;
    while (r == 0 && blen > 0) {
        const size_t k = blen > (1u << 30) ? (1u << 30) : blen;
        if (co_sock_send(fd, body, (int)k, ms) < 0) r = -1;
        body = (const char*)body + k;
        blen -= k;
    }
    const int e = errno;
    co_buf_free(b);
    errno = e;
    return r;
}

int co_http_build_response(co_buf* b, int status, const char* hdrs, size_t hlen,
                           const void* body, size_t blen, int keepalive, int head_request) {
    char line[160];
    struct tm tm;
    const time_t now = time(0);
    if (status < 100 || status > 999) status = 500;
    const int bodiless = status < 200 || status == 204 || status == 304;
    snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", status, co_http_reason(status));
    buf_str(b, line);
    gmtime_r(&now, &tm);
    strftime(line, sizeof(line), "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", &tm);
    buf_str(b, line);
    if (!bodiless) {
        snprintf(line, sizeof(line), "Content-Length: %zu\r\n", blen);
        buf_str(b, line);
    }
    buf_str(b, keepalive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");
    if (hlen) co_buf_append(b, hdrs, hlen);
    buf_str(b, "\r\n");
    if (!head_request && !bodiless && blen) co_buf_append(b, body, blen);
    return b->p ? 0 : -1;
}

int co_http_send_response(int fd, int status, const char* hdrs, size_t hlen,
                          const void* body, size_t blen, int keepalive,
                          int head_request, int ms) {
    co_buf b = {0, 0, 0};
    if (co_http_build_response(&b, status, hdrs, hlen, body, blen, keepalive, head_request) != 0) {
        co_buf_free(&b);
        return -1;
    }
    return send_buf(fd, &b, 0, 0, ms);
}

int co_http_send_request(int fd, const char* method, const char* target,
                         const char* host, const char* hdrs, size_t hlen,
                         const void* body, size_t blen, int ms) {
    co_buf b = {0, 0, 0};
    char line[64];
    const char* p;
    if (*target == 0) target = "/";
    for (p = target; *p; ++p) {
        if ((unsigned char)*p <= 0x20 || *p == 0x7f) {
            errno = EINVAL;
            return -1;
        }
    }
    if (!co_http_field_ok(method, "") || !co_http_field_ok("Host", host)) {
        errno = EINVAL;
        return -1;
    }
    buf_str(&b, method);
    buf_str(&b, " ");
    buf_str(&b, target);
    buf_str(&b, " HTTP/1.1\r\nHost: ");
    buf_str(&b, host);
    buf_str(&b, "\r\n");
    if (blen > 0 || strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 ||
        strcmp(method, "PATCH") == 0) {
        snprintf(line, sizeof(line), "Content-Length: %zu\r\n", blen);
        buf_str(&b, line);
    }
    if (hlen) co_buf_append(&b, hdrs, hlen);
    if (buf_str(&b, "\r\n") != 0) {
        co_buf_free(&b);
        return -1;
    }
    return send_buf(fd, &b, body, blen, ms);
}

int co_http_error_status(int result) {
    switch (result) {
      case CO_HTTP_BODY_TOO_LARGE: return 413;
      case CO_HTTP_HEAD_TOO_LARGE: return 431;
      default: return 400;
    }
}

const char* co_http_result_str(int result, int err) {
    switch (result) {
      case CO_HTTP_OK: return "ok";
      case CO_HTTP_CLOSED: return "connection closed by peer";
      case CO_HTTP_ERR: return err == ETIMEDOUT ? "timed out" : strerror(err);
      case CO_HTTP_BAD: return "malformed or truncated message";
      case CO_HTTP_BODY_TOO_LARGE: return "body too large";
      case CO_HTTP_HEAD_TOO_LARGE: return "head too large";
      default: return "unknown error";
    }
}

const char* co_http_reason(int status) {
    switch (status) {
      case 100: return "Continue";
      case 101: return "Switching Protocols";
      case 200: return "OK";
      case 201: return "Created";
      case 202: return "Accepted";
      case 204: return "No Content";
      case 206: return "Partial Content";
      case 301: return "Moved Permanently";
      case 302: return "Found";
      case 303: return "See Other";
      case 304: return "Not Modified";
      case 307: return "Temporary Redirect";
      case 308: return "Permanent Redirect";
      case 400: return "Bad Request";
      case 401: return "Unauthorized";
      case 403: return "Forbidden";
      case 404: return "Not Found";
      case 405: return "Method Not Allowed";
      case 408: return "Request Timeout";
      case 409: return "Conflict";
      case 411: return "Length Required";
      case 413: return "Content Too Large";
      case 414: return "URI Too Long";
      case 415: return "Unsupported Media Type";
      case 429: return "Too Many Requests";
      case 431: return "Request Header Fields Too Large";
      case 500: return "Internal Server Error";
      case 501: return "Not Implemented";
      case 502: return "Bad Gateway";
      case 503: return "Service Unavailable";
      case 504: return "Gateway Timeout";
      default: return "Unknown";
    }
}

/* ---- URLs -------------------------------------------------------------- */

int co_http_parse_url(const char* url, char* host, size_t hostlen, int* port,
                      int* tls, const char** path) {
    const char *p = url, *h, *he;
    *tls = 0;
    if (strncasecmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncasecmp(p, "https://", 8) == 0) {
        p += 8;
        *tls = 1;
    } else if (strstr(p, "://") != 0) {
        return -1;
    }
    *port = *tls ? 443 : 80;
    if (*p == '[') {
        h = ++p;
        while (*p && *p != ']') p++;
        if (*p != ']') return -1;
        he = p++;
    } else {
        h = p;
        while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') p++;
        he = p;
    }
    if (he == h || (size_t)(he - h) >= hostlen) return -1;
    memcpy(host, h, (size_t)(he - h));
    host[he - h] = 0;
    if (*p == ':') {
        int v = 0, digits = 0;
        for (++p; *p >= '0' && *p <= '9'; ++p) {
            v = v * 10 + (*p - '0');
            if (++digits > 5) return -1;
        }
        if (digits == 0 || v < 1 || v > 65535) return -1;
        *port = v;
    }
    if (*p && *p != '/' && *p != '?' && *p != '#') return -1;
    *path = p;
    return 0;
}

void co_http_host_header(const char* host, int port, int tls, char* out, size_t outlen) {
    const int v6 = strchr(host, ':') != 0;
    const int dflt = port == (tls ? 443 : 80);
    if (dflt) {
        snprintf(out, outlen, v6 ? "[%s]" : "%s", host);
    } else {
        snprintf(out, outlen, v6 ? "[%s]:%d" : "%s:%d", host, port);
    }
}
