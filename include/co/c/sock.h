#pragma once

/* TCP sockets in plain C, wrapped by co/tcp.h.
 *
 * This is compiled by the C compiler directly and never goes through
 * cpprust, which keeps it out of every lowering run.
 *
 * Every fd returned here is non-blocking and close-on-exec, and never raises
 * SIGPIPE. Blocking behaviour comes from poll(): a timeout @ms < 0 waits
 * forever, and a timeout sets errno to ETIMEDOUT. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* listen on @ip:@port (ip NULL or "" means 0.0.0.0, port 0 picks a free
 * one); returns the fd or -1 */
int co_sock_listen(const char* ip, int port, int backlog);

/* the local port an fd is bound to, or -1 */
int co_sock_local_port(int fd);

/* the peer address of a connected fd into @ip (at least 64 bytes) and
 * @port; returns 0 or -1 */
int co_sock_peer(int fd, char* ip, size_t iplen, int* port);

/* wait up to @ms for a connection on listening @fd; returns the new fd or -1 */
int co_sock_accept(int fd, int ms);

/* connect to @host:@port (a name or a numeric v4/v6 address) within @ms;
 * returns the fd or -1. A name that does not resolve sets EHOSTUNREACH. */
int co_sock_connect(const char* host, int port, int ms);

/* >0 bytes read, 0 if the peer closed, -1 on error or timeout */
int co_sock_recv(int fd, void* buf, int n, int ms);

/* read exactly @n bytes, @ms bounding the whole call; returns @n, 0 if the
 * peer closed first, -1 on error or timeout */
int co_sock_recvn(int fd, void* buf, int n, int ms);

/* write all @n bytes, @ms bounding the whole call; returns @n or -1 */
int co_sock_send(int fd, const void* buf, int n, int ms);

/* write what fits without waiting: bytes written (0 if none would fit), or
 * -1 on error */
int co_sock_send_some(int fd, const void* buf, size_t n);

/* @how: 'r', 'w' or 'b' (both); returns 0 or -1 */
int co_sock_shutdown(int fd, char how);

int co_sock_set_nodelay(int fd, int on);
void co_sock_close(int fd);

#ifdef __cplusplus
}
#endif
