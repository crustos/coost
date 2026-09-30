#include "co/http.h"
#include <errno.h>

// The loop calls back into C with C types; this hands the request to the
// Server it belongs to.
static void co_http_dispatch(co_http_conn* c, co_http_msg* m, void* ud) {
    http::Server* s = (http::Server*)ud;
    s->_dispatch(c, m);
}

namespace http {

bool Server::start(const char* ip, int port) {
    this->close();
    if (!_srv.start(ip, port, 1024)) return false;
    _s = co_http_server_new(_srv.fd(), co_http_dispatch, this);
    if (_s == 0) {
        _srv.close();
        return false;
    }
    this->_limits();
    return true;
}

void Server::_dispatch(void* conn, void* msg) {
    Req req;
    req._m = (co_http_msg*)msg;
    _res.reset();
    if (this->_fn) {
        this->_fn(&req, &_res, _ud);
    } else {
        _res.set_status(404);
    }
    co_http_server_respond((co_http_conn*)conn, _res._status, _res._hdrs.data(), _res._hdrs.size(),
                           _res._body.data(), _res._body.size());
}

bool Client::open(const char* url) {
    char host[256];
    char hh[320];
    int port = 0;
    int tls = 0;
    const char* path = 0;
    _c.close();
    _host.clear();
    _err.clear();
    if (co_http_parse_url(url, host, sizeof(host), &port, &tls, &path) != 0) {
        _err.assign_cstr("invalid url");
        return false;
    }
    if (tls) {
        _err.assign_cstr("https is not supported: no TLS in this build");
        return false;
    }
    _host.assign_cstr(host);
    _port = port;
    co_http_host_header(host, port, tls, hh, sizeof(hh));
    _host_hdr.assign_cstr(hh);
    return true;
}

bool Client::request(const char* method, const char* path, const void* body, size_t n) {
    _err.clear();
    co_http_msg_reset(&_res);
    if (_host.empty()) {
        _err.assign_cstr("not opened");
        return false;
    }
    const int head = strcmp(method, "HEAD") == 0 ? 1 : 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool reused = _c.is_open();
        if (!reused) {
            if (!_c.connect(_host.c_str(), _port, _timeout)) {
                _err.assign_cstr(co_http_result_str(CO_HTTP_ERR, errno));
                return false;
            }
            _c.set_nodelay(true);
        }
        int r = CO_HTTP_ERR;
        if (co_http_send_request(_c.fd(), method, path, _host_hdr.c_str(),
                                 _hdrs.data(), _hdrs.size(), body, n, _timeout) == 0) {
            r = co_http_read_response(_c.fd(), &_res, _timeout, _max_body, head);
        }
        const int err = errno;
        if (r == CO_HTTP_OK) {
            if (!_res.keepalive) _c.close();
            return true;
        }
        _c.close();
        // a kept-alive connection the server had already dropped: try once
        // more on a fresh one
        const bool stale = r == CO_HTTP_CLOSED ||
                           (r == CO_HTTP_ERR && (err == EPIPE || err == ECONNRESET));
        if (reused && stale) {
            co_http_msg_reset(&_res);
            continue;
        }
        _err.assign_cstr(co_http_result_str(r, err));
        co_http_msg_reset(&_res);
        return false;
    }
    return false;
}

} // http
