#pragma once

/* HTTP/1.1 in plain C, wrapped by co/http.h.
 *
 * Everything that looks at bytes lives here: the head parser, header
 * lookup, the chunked decoder, and the loops that read a whole message off
 * a socket and write one back. The C++ side only owns objects and calls in.
 *
 * Plain HTTP only; there is no TLS. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* largest head (start line + fields) accepted, and the most header lines */
#define CO_HTTP_MAX_HEAD (64 * 1024)

/* ---- growable byte buffer (always NUL-terminated once allocated) ---- */

typedef struct co_buf {
    char* p;
    size_t len;
    size_t cap;
} co_buf;

int co_buf_reserve(co_buf* b, size_t n);             /* room for n bytes + NUL */
int co_buf_append(co_buf* b, const void* s, size_t n);
void co_buf_consume(co_buf* b, size_t n);             /* drop n bytes from the front */
void co_buf_clear(co_buf* b);
void co_buf_free(co_buf* b);

/* ---- head parsing ---- */

/* Offsets into a head. For a request: method at 0, target; for a response:
 * status and reason. */
typedef struct co_http_head {
    size_t method_len;
    size_t target_off;
    size_t target_len;
    int status;
    size_t reason_off;
    size_t reason_len;
    int minor;        /* 0 or 1: HTTP/1.0 or HTTP/1.1 */
    size_t fields_off;
    size_t len;       /* whole head, including the blank line */
} co_http_head;

/* offset just past the blank line ending a head in s[0..len), or 0 if there
 * is none yet. @from: bytes already scanned, to resume without rescanning. */
size_t co_http_head_end(const char* s, size_t len, size_t from);

/* parse a complete head of @len bytes; 0 ok, -1 malformed. Header lines
 * are validated (no obsolete folding, no space before the colon, no
 * control characters in values). */
int co_http_parse_request_head(const char* s, size_t len, co_http_head* h);
int co_http_parse_response_head(const char* s, size_t len, co_http_head* h);

/* iterate header fields: *pos starts at 0; returns 1 per field, 0 at the
 * end. Values are trimmed of surrounding whitespace. */
int co_http_header_next(const char* s, const co_http_head* h, size_t* pos,
                        const char** name, size_t* nlen,
                        const char** value, size_t* vlen);

/* the first field named @name (case-insensitive) or NULL */
const char* co_http_header_get(const char* s, const co_http_head* h,
                               const char* name, size_t* vlen);

/* 1 if any @name field lists @token in its comma-separated value */
int co_http_header_has_token(const char* s, const co_http_head* h,
                             const char* name, const char* token);

/* Content-Length: -1 absent, -2 invalid or conflicting, else the value */
int64_t co_http_content_length(const char* s, const co_http_head* h);

/* 1 if @name and @value can go into a head as-is (a token, and no CR, LF
 * or other control characters in the value) */
int co_http_field_ok(const char* name, const char* value);

/* ---- chunked transfer coding ---- */

typedef struct co_http_chunked {
    int state;
    int digits;
    size_t left;
    size_t skipped; /* extension and trailer bytes, bounded */
} co_http_chunked;

void co_http_chunked_init(co_http_chunked* d);

/* Decode buf[0..*len) in place, resuming from @d. On return buf[0..*len)
 * is decoded data. Returns -2 if more input is needed, -1 on malformed
 * input, or, once the terminating chunk and trailers are read, the number
 * of bytes that followed the body, left at buf[*len..*len + ret). */
long co_http_chunked_decode(co_http_chunked* d, char* buf, size_t* len);

/* ---- whole messages over a socket ---- */

/* results of co_http_read_* */
#define CO_HTTP_MORE 2              /* co_http_parse only: needs more input */
#define CO_HTTP_OK 1
#define CO_HTTP_CLOSED 0            /* peer closed before sending anything */
#define CO_HTTP_ERR (-1)            /* I/O error or timeout; see errno */
#define CO_HTTP_BAD (-2)            /* malformed or truncated message */
#define CO_HTTP_BODY_TOO_LARGE (-3)
#define CO_HTTP_HEAD_TOO_LARGE (-4)

typedef struct co_http_msg {
    co_buf in;        /* read from the socket, not yet consumed */
    co_buf head;      /* start line and fields of the last message */
    co_buf body;      /* its decoded body */
    co_http_head h;   /* offsets into head */
    int keepalive;    /* the connection may carry another message */
    /* parser state */
    int phase;
    size_t scanned;   /* head bytes already searched for the blank line */
    size_t left;      /* Content-Length bytes still to come */
    co_http_chunked ch;
} co_http_msg;

void co_http_msg_init(co_http_msg* m);
void co_http_msg_reset(co_http_msg* m); /* also drops unconsumed input */
void co_http_msg_free(co_http_msg* m);

/* header of the last message read into @m, or NULL */
const char* co_http_msg_header(const co_http_msg* m, const char* name, size_t* vlen);

/* Parse from m->in without doing any I/O: append received bytes to m->in
 * and call again. @eof: the peer has closed. Returns CO_HTTP_OK with head
 * and body filled in (bytes of a following message stay in m->in),
 * CO_HTTP_MORE, CO_HTTP_CLOSED (eof before any byte) or an error. After an
 * error the connection is unusable. */
int co_http_parse(co_http_msg* m, int is_req, size_t max_body, int head_request, int eof);

/* 1 if part of a message has been received but not completed */
int co_http_msg_busy(const co_http_msg* m);

/* blocking versions over a socket; @ms bounds each wait for data */
int co_http_read_request(int fd, co_http_msg* m, int ms, size_t max_body);
int co_http_read_response(int fd, co_http_msg* m, int ms, size_t max_body, int head_request);

/* append a whole response to @b; see co_http_send_response. 0 or -1 */
int co_http_build_response(co_buf* b, int status, const char* hdrs, size_t hlen,
                           const void* body, size_t blen, int keepalive, int head_request);

/* @hdrs: extra "Name: value\r\n" lines. Content-Length, Connection and
 * Date are written here. Returns 0 or -1 (errno). */
int co_http_send_response(int fd, int status, const char* hdrs, size_t hlen,
                          const void* body, size_t blen, int keepalive,
                          int head_request, int ms);

/* @target must hold no whitespace or control characters (EINVAL) */
int co_http_send_request(int fd, const char* method, const char* target,
                         const char* host, const char* hdrs, size_t hlen,
                         const void* body, size_t blen, int ms);

/* the status a server should answer a failed co_http_read_request with */
int co_http_error_status(int result);

/* a description of a co_http_read_* result; @err is errno for CO_HTTP_ERR */
const char* co_http_result_str(int result, int err);

const char* co_http_reason(int status);

/* ---- URLs ---- */

/* "http://host[:port][/path]", "https://..", or "host[:port][/path]";
 * [v6] addresses are bracketed. Fills @host (without brackets), @port (80 or
 * 443 by default), @tls, and @path (points into @url, may be ""). */
int co_http_parse_url(const char* url, char* host, size_t hostlen, int* port,
                      int* tls, const char** path);

/* the Host header value for @host:@port: the port only when it is not the
 * scheme default, brackets around a v6 address */
void co_http_host_header(const char* host, int port, int tls, char* out, size_t outlen);

#ifdef __cplusplus
}
#endif
