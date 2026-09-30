#pragma once

// TCP client and server sockets, blocking with timeouts.
//
// A thin C++ layer over co/c/sock.h: the classes own file descriptors and
// close them on destruction; the socket work itself is compiled as C.
// Timeouts are in milliseconds and a negative timeout waits forever; a
// timeout fails the call with errno set to ETIMEDOUT.

#include "def.h"
#include "fastring.h"
#include "c/sock.h"
#include <string.h>

namespace tcp {

// A connected socket.
class Conn {
  public:
    Conn() { _fd = -1; }
    Conn(const Conn& c) = delete;
    void operator=(const Conn& c) = delete;
    ~Conn() { this->close(); }

    // connect to @host:@port, a name or a numeric v4/v6 address
    bool connect(const char* host, int port, int ms) {
        this->close();
        _fd = co_sock_connect(host, port, ms);
        return _fd >= 0;
    }

    // take ownership of @fd, closing the current one
    void attach(int fd) {
        this->close();
        _fd = fd;
    }

    // give up ownership without closing
    int detach() {
        const int fd = _fd;
        _fd = -1;
        return fd;
    }

    int fd() const { return _fd; }
    bool is_open() const { return _fd >= 0; }

    // >0 bytes read, 0 if the peer closed, -1 on error or timeout
    int recv(void* buf, int n, int ms) { return co_sock_recv(_fd, buf, n, ms); }

    // exactly @n bytes within @ms: @n, 0 if the peer closed first, or -1
    int recvn(void* buf, int n, int ms) { return co_sock_recvn(_fd, buf, n, ms); }

    // all @n bytes within @ms: @n or -1
    int send(const void* buf, int n, int ms) { return co_sock_send(_fd, buf, n, ms); }
    int send_cstr(const char* s, int ms) { return co_sock_send(_fd, s, (int)strlen(s), ms); }
    int send_str(const fastring& s, int ms) { return co_sock_send(_fd, s.data(), (int)s.size(), ms); }

    bool set_nodelay(bool on) { return co_sock_set_nodelay(_fd, on ? 1 : 0) == 0; }

    // @how: 'r', 'w' or 'b'
    bool shutdown(char how) { return co_sock_shutdown(_fd, how) == 0; }

    // "ip:port" of the peer, "" if unknown
    fastring peer() const {
        char ip[64];
        int port = 0;
        fastring s;
        if (co_sock_peer(_fd, ip, sizeof(ip), &port) == 0) {
            s.append_cstr(ip);
            s.append_char(':');
            s.append_int(port);
        }
        return s;
    }

    void close() {
        if (_fd >= 0) {
            co_sock_close(_fd);
            _fd = -1;
        }
    }

  private:
    int _fd;
};

// A listening socket.
class Server {
  public:
    Server() { _fd = -1; }
    Server(const Server& s) = delete;
    void operator=(const Server& s) = delete;
    ~Server() { this->close(); }

    // listen on @ip:@port; ip NULL or "" means all v4 addresses, port 0
    // picks a free port (see port())
    bool start(const char* ip, int port, int backlog) {
        this->close();
        _fd = co_sock_listen(ip, port, backlog);
        return _fd >= 0;
    }

    // the bound port, or -1
    int port() const { return _fd >= 0 ? co_sock_local_port(_fd) : -1; }
    int fd() const { return _fd; }
    bool is_open() const { return _fd >= 0; }

    // wait up to @ms for a connection and hand it to @c
    bool accept(Conn* c, int ms) {
        const int fd = co_sock_accept(_fd, ms);
        if (fd < 0) return false;
        c->attach(fd);
        return true;
    }

    void close() {
        if (_fd >= 0) {
            co_sock_close(_fd);
            _fd = -1;
        }
    }

  private:
    int _fd;
};

} // tcp
