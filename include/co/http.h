#pragma once

// HTTP/1.1 client and server, blocking with timeouts. Plain HTTP only.
//
// The protocol (parsing, chunked decoding, reading and writing whole
// messages) is compiled as C in co/c/http.h; these classes own the sockets
// and buffers and present them.
//
// The server serves many connections at once from one thread: a poll()
// loop in co/c/http_server.h. Handlers run on that loop and must not block.

#include "def.h"
#include "fastring.h"
#include "tcp.h"
#include "c/http.h"
#include "c/http_server.h"
#include <string.h>

namespace http {

// A request as the server received it; valid during the handler call.
class Req {
  public:
    Req() { _m = 0; }
    Req(const Req& r) = delete;
    void operator=(const Req& r) = delete;

    fastring method() const { return fastring(_m->head.p, _m->h.method_len); }
    bool method_is(const char* m) const {
        return strlen(m) == _m->h.method_len && memcmp(_m->head.p, m, _m->h.method_len) == 0;
    }

    // the request target as sent, e.g. "/a/b?x=1"
    fastring url() const { return fastring(_m->head.p + _m->h.target_off, _m->h.target_len); }

    // the target up to '?', and what follows it ("" if nothing)
    fastring path() const {
        const char* t = _m->head.p + _m->h.target_off;
        const char* q = (const char*)memchr(t, '?', _m->h.target_len);
        if (q == 0) return fastring(t, _m->h.target_len);
        return fastring(t, (size_t)(q - t));
    }
    fastring query() const {
        const char* t = _m->head.p + _m->h.target_off;
        const char* q = (const char*)memchr(t, '?', _m->h.target_len);
        if (q == 0) return fastring();
        return fastring(q + 1, _m->h.target_len - (size_t)(q + 1 - t));
    }

    // 0 for HTTP/1.0, 1 for HTTP/1.1
    int version() const { return _m->h.minor; }

    // the first header named @name (case-insensitive), "" if absent
    fastring header(const char* name) const {
        size_t n = 0;
        const char* v = co_http_msg_header(_m, name, &n);
        if (v == 0) return fastring();
        return fastring(v, n);
    }
    bool has_header(const char* name) const {
        size_t n = 0;
        return co_http_msg_header(_m, name, &n) != 0;
    }

    // the decoded body, NUL-terminated
    const char* body() const { return _m->body.p ? _m->body.p : ""; }
    size_t body_size() const { return _m->body.len; }

    // used by the server
    co_http_msg* _m;
};

// The response a handler fills in. Status defaults to 200.
class Res {
  public:
    Res() { _status = 200; }

    void set_status(int s) { _status = s; }
    int status() const { return _status; }

    // false if @name or @value would break the head. Content-Length,
    // Connection and Date are written by the server; don't add them.
    bool add_header(const char* name, const char* value) {
        if (!co_http_field_ok(name, value)) return false;
        _hdrs.append_cstr(name);
        _hdrs.append(": ", 2);
        _hdrs.append_cstr(value);
        _hdrs.append("\r\n", 2);
        return true;
    }

    void set_body(const void* s, size_t n) { _body.assign(s, n); }
    void set_body_cstr(const char* s) { _body.assign_cstr(s); }
    void set_body_str(const fastring& s) { _body.assign(s.data(), s.size()); }

    void reset() {
        _status = 200;
        _hdrs.clear();
        _body.clear();
    }

    // used by the server
    int _status;
    fastring _hdrs;
    fastring _body;
};

class Server {
  public:
    Server() {
        this->_fn = 0;
        _ud = 0;
        _s = 0;
        _timeout = 5000;
        _max_body = 8 << 20;
        _max_conns = 1024;
    }
    Server(const Server& s) = delete;
    void operator=(const Server& s) = delete;
    ~Server() { this->close(); }

    // @fn is called for every request, with @ud passed through. Without a
    // handler every request gets 404.
    void on_req(void (*fn)(const Req* req, Res* res, void* ud), void* ud) {
        this->_fn = fn;
        _ud = ud;
    }

    // listen on @ip:@port; port 0 picks a free port (see port())
    bool start(const char* ip, int port);
    int port() const { return _srv.port(); }

    // bounds idling between requests, receiving one request from its first
    // byte, and a stalled write (default 5000 ms)
    void set_timeout(int ms) {
        _timeout = ms;
        this->_limits();
    }

    // larger bodies are answered with 413 (default 8 MB)
    void set_max_body(size_t n) {
        _max_body = n;
        this->_limits();
    }

    // further clients wait in the listen backlog (default 1024)
    void set_max_conns(int n) {
        _max_conns = n;
        this->_limits();
    }

    // one round of the loop: wait up to @ms for activity and handle it.
    // Returns the number of requests answered, or -1.
    int step(int ms) { return _s ? co_http_server_step(_s, ms) : -1; }

    // serve until stop() is called (from a handler) and every response
    // has been written
    void run() {
        while (_s && !co_http_server_done(_s)) {
            if (co_http_server_step(_s, 1000) < 0) break;
        }
    }

    // stop accepting and reading; the current response gets
    // "Connection: close", and connections close once written
    void stop() {
        if (_s) co_http_server_stop(_s);
    }
    bool stopped() const { return _s == 0 || co_http_server_done(_s) != 0; }

    // open connections, and connections accepted so far
    int conns() const { return _s ? co_http_server_conns(_s) : 0; }
    long accepted() const { return _s ? co_http_server_accepted(_s) : 0; }

    void close() {
        if (_s) {
            co_http_server_free(_s);
            _s = 0;
        }
        _srv.close();
    }

    // called from the loop
    void _dispatch(void* conn, void* msg);

  private:
    void _limits() {
        if (_s) co_http_server_set_limits(_s, _timeout, _max_body, _max_conns);
    }

    tcp::Server _srv;
    co_http_server* _s;
    void (*_fn)(const Req* req, Res* res, void* ud);
    void* _ud;
    Res _res;
    int _timeout;
    size_t _max_body;
    int _max_conns;
};

class Client {
  public:
    Client() {
        co_http_msg_init(&_res);
        _port = 80;
        _timeout = 5000;
        _max_body = 64 << 20;
    }
    Client(const Client& c) = delete;
    void operator=(const Client& c) = delete;
    ~Client() { co_http_msg_free(&_res); }

    // "http://host[:port]" or "host[:port]"; any path is ignored. https is
    // refused: there is no TLS.
    bool open(const char* url);

    // for connecting and for each wait for data (default 5000 ms)
    void set_timeout(int ms) { _timeout = ms; }

    // larger response bodies fail the request (default 64 MB)
    void set_max_body(size_t n) { _max_body = n; }

    // a header sent with every request until clear_headers(); false if
    // @name or @value would break the head
    bool add_header(const char* name, const char* value) {
        if (!co_http_field_ok(name, value)) return false;
        _hdrs.append_cstr(name);
        _hdrs.append(": ", 2);
        _hdrs.append_cstr(value);
        _hdrs.append("\r\n", 2);
        return true;
    }
    void clear_headers() { _hdrs.clear(); }

    // true once a whole response is read; then see status(), body(), header()
    bool get(const char* path) { return this->request("GET", path, 0, 0); }
    bool head(const char* path) { return this->request("HEAD", path, 0, 0); }
    bool del(const char* path) { return this->request("DELETE", path, 0, 0); }
    bool post(const char* path, const void* body, size_t n) { return this->request("POST", path, body, n); }
    bool post_cstr(const char* path, const char* body) { return this->request("POST", path, body, strlen(body)); }
    bool put(const char* path, const void* body, size_t n) { return this->request("PUT", path, body, n); }
    bool request(const char* method, const char* path, const void* body, size_t n);

    int status() const { return _res.h.status; }
    const char* body() const { return _res.body.p ? _res.body.p : ""; }
    size_t body_size() const { return _res.body.len; }
    fastring header(const char* name) const {
        size_t n = 0;
        const char* v = co_http_msg_header(&_res, name, &n);
        if (v == 0) return fastring();
        return fastring(v, n);
    }

    // why the last call failed
    const char* error() const { return _err.c_str(); }

    void close() { _c.close(); }

  private:
    tcp::Conn _c;
    fastring _host;
    fastring _host_hdr;
    int _port;
    int _timeout;
    size_t _max_body;
    fastring _hdrs;
    fastring _err;
    co_http_msg _res;
};

} // http
