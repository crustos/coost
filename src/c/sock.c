#include "co/c/sock.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0 /* SO_NOSIGPIPE is set on the fd instead */
#endif

static int64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int64_t deadline(int ms) { return ms < 0 ? -1 : mono_ms() + ms; }

/* poll until @fd is ready for @events or @dl passes: 1 ready, 0 timed out
 * (errno ETIMEDOUT), -1 error. Readiness includes error and hang-up; the
 * syscall that follows reports those. */
static int wait_until(int fd, short events, int64_t dl) {
    struct pollfd p;
    p.fd = fd;
    p.events = events;
    for (;;) {
        int ms = -1;
        if (dl >= 0) {
            const int64_t left = dl - mono_ms();
            ms = left > 0 ? (int)left : 0;
        }
        p.revents = 0;
        const int r = poll(&p, 1, ms);
        if (r > 0) return 1;
        if (r == 0) {
            errno = ETIMEDOUT;
            return 0;
        }
        if (errno != EINTR) return -1;
    }
}

static void prepare(int fd) {
    const int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

static int resolve(const char* host, int port, int passive, struct addrinfo** res) {
    struct addrinfo hints;
    char ps[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV | (passive ? AI_PASSIVE : 0);
    if (port < 0 || port > 65535) {
        errno = EINVAL;
        return -1;
    }
    snprintf(ps, sizeof(ps), "%d", port);
    if (getaddrinfo(host, ps, &hints, res) != 0) {
        errno = EHOSTUNREACH;
        return -1;
    }
    return 0;
}

int co_sock_listen(const char* ip, int port, int backlog) {
    struct addrinfo *res = 0, *ai;
    int fd = -1, err = EADDRNOTAVAIL, one = 1;
    if (ip == 0 || *ip == 0) ip = "0.0.0.0";
    if (resolve(ip, port, 1, &res) != 0) return -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            err = errno;
            continue;
        }
        prepare(fd);
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 &&
            listen(fd, backlog > 0 ? backlog : 128) == 0) {
            break;
        }
        err = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) errno = err;
    return fd;
}

static int addr_port(const struct sockaddr_storage* ss, char* ip, size_t iplen) {
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in* a = (const struct sockaddr_in*)ss;
        if (ip) inet_ntop(AF_INET, &a->sin_addr, ip, (socklen_t)iplen);
        return ntohs(a->sin_port);
    }
    if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6* a = (const struct sockaddr_in6*)ss;
        if (ip) inet_ntop(AF_INET6, &a->sin6_addr, ip, (socklen_t)iplen);
        return ntohs(a->sin6_port);
    }
    return -1;
}

int co_sock_local_port(int fd) {
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getsockname(fd, (struct sockaddr*)&ss, &sl) != 0) return -1;
    return addr_port(&ss, 0, 0);
}

int co_sock_peer(int fd, char* ip, size_t iplen, int* port) {
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getpeername(fd, (struct sockaddr*)&ss, &sl) != 0) return -1;
    const int p = addr_port(&ss, ip, iplen);
    if (p < 0) return -1;
    if (port) *port = p;
    return 0;
}

int co_sock_accept(int fd, int ms) {
    const int64_t dl = deadline(ms);
    for (;;) {
        const int c = accept(fd, 0, 0);
        if (c >= 0) {
            /* BSDs pass O_NONBLOCK on to the accepted fd, Linux does not */
            prepare(c);
            return c;
        }
        if (errno == EINTR || errno == ECONNABORTED) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        if (wait_until(fd, POLLIN, dl) <= 0) return -1;
    }
}

int co_sock_connect(const char* host, int port, int ms) {
    struct addrinfo *res = 0, *ai;
    const int64_t dl = deadline(ms);
    int fd = -1, err = ECONNREFUSED;
    if (resolve(host, port, 0, &res) != 0) return -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            err = errno;
            continue;
        }
        prepare(fd);
        int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (r != 0 && errno == EINPROGRESS) {
            r = wait_until(fd, POLLOUT, dl) > 0 ? 0 : -1;
            if (r == 0) {
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                if (soerr != 0) {
                    errno = soerr;
                    r = -1;
                }
            }
        }
        if (r == 0) break;
        err = errno;
        close(fd);
        fd = -1;
        if (err == ETIMEDOUT) break; /* the time is spent; don't try more */
    }
    freeaddrinfo(res);
    if (fd < 0) errno = err;
    return fd;
}

static int recv_until(int fd, void* buf, int n, int64_t dl) {
    for (;;) {
        const ssize_t r = recv(fd, buf, (size_t)n, 0);
        if (r >= 0) return (int)r;
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        if (wait_until(fd, POLLIN, dl) <= 0) return -1;
    }
}

int co_sock_recv(int fd, void* buf, int n, int ms) {
    return recv_until(fd, buf, n, deadline(ms));
}

int co_sock_recvn(int fd, void* buf, int n, int ms) {
    const int64_t dl = deadline(ms);
    char* p = (char*)buf;
    int done = 0;
    while (done < n) {
        const int r = recv_until(fd, p + done, n - done, dl);
        if (r <= 0) return r;
        done += r;
    }
    return n;
}

int co_sock_send(int fd, const void* buf, int n, int ms) {
    const int64_t dl = deadline(ms);
    const char* p = (const char*)buf;
    int done = 0;
    while (done < n) {
        const ssize_t r = send(fd, p + done, (size_t)(n - done), SEND_FLAGS);
        if (r >= 0) {
            done += (int)r;
            continue;
        }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        if (wait_until(fd, POLLOUT, dl) <= 0) return -1;
    }
    return n;
}

int co_sock_send_some(int fd, const void* buf, size_t n) {
    for (;;) {
        const ssize_t r = send(fd, buf, n, SEND_FLAGS);
        if (r >= 0) return (int)r;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
}

int co_sock_shutdown(int fd, char how) {
    const int h = how == 'r' ? SHUT_RD : how == 'w' ? SHUT_WR : SHUT_RDWR;
    return shutdown(fd, h);
}

int co_sock_set_nodelay(int fd, int on) {
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

void co_sock_close(int fd) {
    if (fd >= 0) close(fd);
}
