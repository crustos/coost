#pragma once

/* A single-threaded HTTP/1.1 server over poll(), wrapped by http::Server.
 *
 * Many connections are served at once without threads or coroutines: each
 * step polls every connection, reads what has arrived, parses it with
 * co_http_parse, calls the handler for each complete request, and writes
 * responses as far as the socket takes them. A slow or idle client costs a
 * pollfd, not the server's attention.
 *
 * Handlers run on the loop and must not block. */

#include <stddef.h>
#include "http.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct co_http_server co_http_server;
typedef struct co_http_conn co_http_conn;

/* @on_req is called for each complete request and must answer it with
 * co_http_server_respond before returning (otherwise the loop answers
 * 500). @lfd is a listening socket from co_sock_listen; it stays owned by
 * the caller. */
co_http_server* co_http_server_new(int lfd,
                                   void (*on_req)(co_http_conn* c, co_http_msg* m, void* ud),
                                   void* ud);

/* closes every connection, not the listening socket */
void co_http_server_free(co_http_server* s);

/* @timeout_ms bounds, per connection: idling between requests, receiving
 * one request from its first byte, and a stalled write.
 * Defaults: 5000 ms, 8 MB, 1024 connections. */
void co_http_server_set_limits(co_http_server* s, int timeout_ms, size_t max_body, int max_conns);

/* one round: wait up to @ms for activity, then handle it. Returns the
 * number of requests answered, or -1 if poll failed. */
int co_http_server_step(co_http_server* s, int ms);

/* queue the response to the request being handled on @c */
void co_http_server_respond(co_http_conn* c, int status, const char* hdrs, size_t hlen,
                            const void* body, size_t blen);

/* stop accepting; connections close once their responses are written */
void co_http_server_stop(co_http_server* s);

/* stopped and every connection closed */
int co_http_server_done(const co_http_server* s);

/* open connections, and connections accepted so far */
int co_http_server_conns(const co_http_server* s);
long co_http_server_accepted(const co_http_server* s);

#ifdef __cplusplus
}
#endif
