#include "test.h"
#include "co/http.h"
#include "co/time.h"
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

// The socket tests fork: the child is the client and reports through its
// exit status, as in crust's examples/rpython2c/net/http_server.py.

static int wait_child(int pid) {
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 99;
}

static void child_exit(int fails_before) {
    fflush(stdout);
    _exit(g_fails > fails_before ? 1 : 0);
}

// ---- the C protocol core, no sockets ------------------------------------

static bool parse_req(const char* s, co_http_head* h) {
    return co_http_parse_request_head(s, strlen(s), h) == 0;
}

static void test_parser() {
    co_http_head h;
    const char* r = "GET /a?b=1 HTTP/1.1\r\nHost: x\r\nConnection: keep-alive, Upgrade\r\n"
                    "Content-Length: 12\r\nX-Pad:   v  \r\n\r\n";
    CHECK(co_http_head_end(r, strlen(r), 0) == strlen(r));
    CHECK(co_http_head_end(r, strlen(r) - 1, 0) == 0);
    CHECK(parse_req(r, &h));
    CHECK(h.method_len == 3 && h.minor == 1);
    CHECK(h.target_len == 6 && memcmp(r + h.target_off, "/a?b=1", 6) == 0);
    size_t n = 0;
    const char* v = co_http_header_get(r, &h, "x-pad", &n);
    CHECK(v != 0 && n == 1 && *v == 'v');
    CHECK(co_http_header_get(r, &h, "missing", &n) == 0);
    CHECK(co_http_header_has_token(r, &h, "connection", "upgrade") == 1);
    CHECK(co_http_header_has_token(r, &h, "connection", "close") == 0);
    CHECK(co_http_content_length(r, &h) == 12);

    // bare LF line endings are accepted
    CHECK(parse_req("GET / HTTP/1.0\nA: b\n\n", &h));
    CHECK(h.minor == 0);

    // rejected
    CHECK(!parse_req("GET / HTTP/1.1\r\nBad Name: x\r\n\r\n", &h));   // space in name
    CHECK(!parse_req("GET / HTTP/1.1\r\nName : x\r\n\r\n", &h));      // space before colon
    CHECK(!parse_req("GET / HTTP/1.1\r\nA: b\r\n c\r\n\r\n", &h));    // obs-fold
    CHECK(!parse_req("GET / HTTP/2.0\r\n\r\n", &h));
    CHECK(!parse_req("GET  / HTTP/1.1\r\n\r\n", &h));
    CHECK(!parse_req("GET /\r\n\r\n", &h));
    CHECK(!parse_req("GET / HTTP/1.1\r\nA: b\x01\r\n\r\n", &h));

    const char* cl2 = "GET / HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\n";
    CHECK(parse_req(cl2, &h));
    CHECK(co_http_content_length(cl2, &h) == -2);
    const char* clx = "GET / HTTP/1.1\r\nContent-Length: 3x\r\n\r\n";
    CHECK(parse_req(clx, &h));
    CHECK(co_http_content_length(clx, &h) == -2);

    const char* res = "HTTP/1.1 404 Not Found\r\nA: b\r\n\r\n";
    CHECK(co_http_parse_response_head(res, strlen(res), &h) == 0);
    CHECK(h.status == 404 && h.reason_len == 9);
    const char* res2 = "HTTP/1.1 200\r\n\r\n";
    CHECK(co_http_parse_response_head(res2, strlen(res2), &h) == 0);
    CHECK(h.status == 200 && h.reason_len == 0);

    CHECK(co_http_field_ok("X-A", "ok value"));
    CHECK(!co_http_field_ok("X-A", "a\r\nInjected: 1"));
    CHECK(!co_http_field_ok("X A", "v"));
}

static void test_chunked() {
    const char* raw = "5;ext=1\r\nhello\r\n6\r\n world\r\n0\r\nTrailer: x\r\n\r\nNEXT";
    char buf[128];

    // all at once
    co_http_chunked d;
    co_http_chunked_init(&d);
    size_t n = strlen(raw);
    memcpy(buf, raw, n);
    long r = co_http_chunked_decode(&d, buf, &n);
    CHECK(r == 4);
    CHECK(n == 11 && memcmp(buf, "hello world", 11) == 0);
    CHECK(memcmp(buf + n, "NEXT", 4) == 0);

    // one byte at a time, the way it may come off a socket
    co_http_chunked_init(&d);
    char out[128];
    size_t total = 0;
    r = -2;
    for (size_t i = 0; i < strlen(raw) && r == -2; ++i) {
        size_t k = 1;
        out[total] = raw[i];
        r = co_http_chunked_decode(&d, out + total, &k);
        total += k;
    }
    CHECK(r == 0);
    CHECK(total == 11 && memcmp(out, "hello world", 11) == 0);

    co_http_chunked_init(&d);
    n = 4;
    memcpy(buf, "zz\r\n", 4);
    CHECK(co_http_chunked_decode(&d, buf, &n) == -1);
    co_http_chunked_init(&d);
    n = 18;
    memcpy(buf, "fffffffffffffffff\n", 18); // overflows
    CHECK(co_http_chunked_decode(&d, buf, &n) == -1);
}

static void test_url() {
    char host[64];
    int port = 0;
    int tls = 0;
    const char* path = 0;
    CHECK(co_http_parse_url("http://example.com/x?y", host, sizeof(host), &port, &tls, &path) == 0);
    CHECK_STR(host, "example.com");
    CHECK(port == 80 && tls == 0);
    CHECK_STR(path, "/x?y");
    CHECK(co_http_parse_url("HTTPS://h:8443", host, sizeof(host), &port, &tls, &path) == 0);
    CHECK(port == 8443 && tls == 1 && *path == 0);
    CHECK(co_http_parse_url("[::1]:81/p", host, sizeof(host), &port, &tls, &path) == 0);
    CHECK_STR(host, "::1");
    CHECK(port == 81);
    CHECK(co_http_parse_url("ftp://h", host, sizeof(host), &port, &tls, &path) != 0);
    CHECK(co_http_parse_url("h:0", host, sizeof(host), &port, &tls, &path) != 0);
    CHECK(co_http_parse_url("h:99999", host, sizeof(host), &port, &tls, &path) != 0);
    CHECK(co_http_parse_url("http://", host, sizeof(host), &port, &tls, &path) != 0);

    char hh[64];
    co_http_host_header("::1", 81, 0, hh, sizeof(hh));
    CHECK_STR(hh, "[::1]:81");
    co_http_host_header("a.b", 80, 0, hh, sizeof(hh));
    CHECK_STR(hh, "a.b");
}

// ---- our server, our client and raw requests ----------------------------

class State {
  public:
    http::Server* srv;
    int requests;
    bool stopped;
    char* big;
    size_t big_n;
};

static void handle(const http::Req* req, http::Res* res, void* ud) {
    State* st = (State*)ud;
    st->requests++;
    fastring path = req->path();
    if (path.eq_cstr("/")) {
        res->add_header("Content-Type", "text/plain");
        res->set_body_cstr("hello");
    } else if (path.eq_cstr("/echo")) {
        fastring m = req->method();
        fastring q = req->query();
        res->add_header("X-Method", m.c_str());
        res->add_header("X-Query", q.c_str());
        res->set_body(req->body(), req->body_size());
    } else if (path.eq_cstr("/hdr")) {
        fastring t = req->header("x-token");
        res->set_body_str(t);
        if (res->add_header("X-Bad", "a\r\nb")) res->set_status(500);
    } else if (path.eq_cstr("/big")) {
        res->set_body(st->big, st->big_n);
    } else if (path.eq_cstr("/stop")) {
        st->stopped = true;
        st->srv->stop();
        res->set_body_cstr("bye");
    } else {
        res->set_status(404);
    }
}

// send @req on a fresh connection and read until the server closes
static fastring raw_exchange(int port, const char* req) {
    tcp::Conn c;
    fastring out;
    char buf[4096];
    if (!c.connect("127.0.0.1", port, 2000)) {
        return out;
    }
    c.send_cstr(req, 2000);
    for (;;) {
        const int r = c.recv(buf, sizeof(buf), 2000);
        if (r <= 0) break;
        out.append(buf, (size_t)r);
    }
    return out;
}

static void client_side(int port) {
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d", port);

    // one keep-alive connection for all of these
    http::Client cli;
    CHECK(cli.open(url));
    CHECK(cli.get("/"));
    CHECK(cli.status() == 200);
    CHECK_STR(cli.body(), "hello");
    fastring ct = cli.header("content-type");
    CHECK(ct.eq_cstr("text/plain"));
    fastring conn = cli.header("Connection");
    CHECK(conn.eq_cstr("keep-alive"));
    fastring date = cli.header("date");
    CHECK(date.ends_with_cstr(" GMT"));

    CHECK(cli.post_cstr("/echo?x=1", "payload"));
    CHECK_STR(cli.body(), "payload");
    fastring m = cli.header("x-method");
    CHECK(m.eq_cstr("POST"));
    fastring q = cli.header("x-query");
    CHECK(q.eq_cstr("x=1"));

    CHECK(cli.head("/"));
    CHECK(cli.status() == 200 && cli.body_size() == 0);
    fastring len = cli.header("content-length");
    CHECK(len.eq_cstr("5"));

    CHECK(cli.get("/missing"));
    CHECK(cli.status() == 404);

    CHECK(cli.add_header("X-Token", "t0k"));
    CHECK(!cli.add_header("X-Bad", "a\r\nb"));
    CHECK(cli.get("/hdr"));
    CHECK(cli.status() == 200);
    CHECK_STR(cli.body(), "t0k");
    cli.clear_headers();

    CHECK(cli.put("/echo", "p", 1));
    m = cli.header("x-method");
    CHECK(m.eq_cstr("PUT") && cli.body_size() == 1);
    CHECK(cli.del("/echo"));
    m = cli.header("x-method");
    CHECK(m.eq_cstr("DELETE") && cli.body_size() == 0);

    const size_t big = 1 << 20;
    char* p = (char*)malloc(big);
    for (size_t i = 0; i < big; ++i) p[i] = (char)('a' + i % 26);
    CHECK(cli.post("/echo", p, big));
    CHECK(cli.body_size() == big && memcmp(cli.body(), p, big) == 0);
    free(p);
    cli.close();

    // a chunked request body
    fastring r = raw_exchange(port,
        "POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n"
        "Connection: close\r\n\r\n3\r\nabc\r\n2;e\r\nde\r\n0\r\n\r\n");
    CHECK(r.starts_with_cstr("HTTP/1.1 200 OK\r\n"));
    CHECK(r.ends_with_cstr("\r\n\r\nabcde"));
    CHECK(r.contains_cstr("Connection: close\r\n"));

    // malformed requests
    r = raw_exchange(port, "GET / HTTP/1.1\r\nBad Header: x\r\n\r\n");
    CHECK(r.starts_with_cstr("HTTP/1.1 400 "));
    r = raw_exchange(port, "POST /echo HTTP/1.1\r\nContent-Length: 5000000\r\n\r\n");
    CHECK(r.starts_with_cstr("HTTP/1.1 413 "));
    r = raw_exchange(port, "POST /echo HTTP/1.1\r\nContent-Length: 1\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    CHECK(r.starts_with_cstr("HTTP/1.1 400 "));

    // pipelined: both answered, in order, on one connection
    r = raw_exchange(port, "GET / HTTP/1.1\r\nHost: x\r\n\r\n"
                           "GET /missing HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    const size_t second = r.find_cstr("HTTP/1.1 404");
    CHECK(r.starts_with_cstr("HTTP/1.1 200 OK\r\n"));
    CHECK(second != fastring::npos);

    // HTTP/1.0 without keep-alive is closed after one response
    r = raw_exchange(port, "GET / HTTP/1.0\r\n\r\n");
    CHECK(r.contains_cstr("Connection: close\r\n"));
    CHECK(r.ends_with_cstr("hello"));

    http::Client last;
    CHECK(last.open(url));
    CHECK(last.get("/stop"));
    CHECK_STR(last.body(), "bye");
    conn = last.header("connection");
    CHECK(conn.eq_cstr("close"));
}

static void test_server() {
    http::Server srv;
    State st;
    st.srv = &srv;
    st.requests = 0;
    st.stopped = false;
    st.big = 0;
    st.big_n = 0;
    srv.on_req(handle, &st);
    srv.set_max_body(2 << 20);
    CHECK(srv.start("127.0.0.1", 0));
    const int port = srv.port();
    CHECK(port > 0);

    fflush(stdout);
    const int pid = fork();
    if (pid == 0) {
        const int before = g_fails;
        srv.close();
        client_side(port);
        child_exit(before);
    }

    co::Timer t;
    while (!srv.stopped() && t.ms() < 30000) srv.step(50);
    CHECK(wait_child(pid) == 0);
    CHECK(st.stopped && srv.stopped());
    // keep-alive: 9 requests on the first connection, then 7 more
    CHECK(srv.accepted() == 8);
    CHECK(st.requests == 13);
}

// ---- many clients at once on one loop ------------------------------------

static const char* GET = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";

static void concurrent_client(int port) {
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d", port);

    // an idle connection and a half-sent request hold connections open
    tcp::Conn idle;
    tcp::Conn half;
    CHECK(idle.connect("127.0.0.1", port, 2000));
    CHECK(half.connect("127.0.0.1", port, 2000));
    CHECK(half.send_cstr("GET / HTTP/1.1\r\nHo", 2000) > 0);

    // ... and a normal client is served anyway
    http::Client cli;
    CHECK(cli.open(url));
    co::Timer t;
    CHECK(cli.get("/"));
    CHECK_STR(cli.body(), "hello");
    CHECK(t.ms() < 500);

    // the half-sent request completes and is answered
    char buf[4096];
    CHECK(half.send_cstr("st: x\r\nConnection: close\r\n\r\n", 2000) > 0);
    int n = half.recv(buf, sizeof(buf) - 1, 2000);
    CHECK(n > 0);
    if (n > 0) buf[n] = 0;
    CHECK(n > 0 && strstr(buf, "HTTP/1.1 200 OK") == buf);

    // 50 connections send before any response is read
    tcp::Conn many[50];
    int sent = 0;
    for (int i = 0; i < 50; ++i) {
        if (many[i].connect("127.0.0.1", port, 2000) &&
            many[i].send_cstr(GET, 2000) == (int)strlen(GET)) {
            sent++;
        }
    }
    CHECK(sent == 50);
    int ok = 0;
    for (int i = 0; i < 50; ++i) {
        n = many[i].recv(buf, sizeof(buf) - 1, 2000);
        if (n > 0) {
            buf[n] = 0;
            if (strstr(buf, "HTTP/1.1 200 OK") == buf && strstr(buf, "hello")) ok++;
        }
    }
    CHECK(ok == 50);
    // still open until here (their destructors run at scope exit), so all
    // 50 were open on the server at once

    // a client that doesn't read a 4 MB response doesn't hold up others
    tcp::Conn lazy;
    CHECK(lazy.connect("127.0.0.1", port, 2000));
    CHECK(lazy.send_cstr("GET /big HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", 2000) > 0);
    co::sleep_ms(100);
    t.restart();
    CHECK(cli.get("/"));
    CHECK(t.ms() < 500);
    size_t got = 0;
    for (;;) {
        n = lazy.recv(buf, sizeof(buf), 2000);
        if (n <= 0) break;
        got += (size_t)n;
    }
    CHECK(got > (4u << 20) && got < (4u << 20) + 512); // head + body

    // the idle connection is dropped once the timeout (1 s) passes
    n = idle.recv(buf, sizeof(buf), 3000);
    CHECK(n == 0);

    // a request trickling in a byte at a time is cut off a timeout after
    // its first byte, however steadily it trickles
    tcp::Conn slow;
    CHECK(slow.connect("127.0.0.1", port, 2000));
    t.restart();
    const char* req = "GET /aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa HTTP/1.1\r\n\r\n";
    bool cut = false;
    for (size_t i = 0; i < strlen(req) && !cut; ++i) {
        if (slow.send(req + i, 1, 1000) != 1) cut = true;
        if (!cut && slow.recv(buf, sizeof(buf), 200) == 0) cut = true;
    }
    CHECK(cut);
    CHECK(t.ms() < 2500);

    cli.close();
    http::Client last;
    CHECK(last.open(url));
    CHECK(last.get("/stop"));
    CHECK_STR(last.body(), "bye");
}

static void test_concurrent() {
    http::Server srv;
    State st;
    st.srv = &srv;
    st.requests = 0;
    st.stopped = false;
    st.big_n = 4 << 20;
    st.big = (char*)malloc(st.big_n);
    memset(st.big, 'z', st.big_n);
    srv.on_req(handle, &st);
    srv.set_timeout(1000);
    CHECK(srv.start("127.0.0.1", 0));
    const int port = srv.port();

    fflush(stdout);
    const int pid = fork();
    if (pid == 0) {
        const int before = g_fails;
        srv.close();
        concurrent_client(port);
        child_exit(before);
    }

    // bounded by time, not steps: a busy loop takes many short steps
    int peak = 0;
    long steps = 0;
    co::Timer t;
    while (!srv.stopped() && t.ms() < 30000) {
        srv.step(50);
        steps++;
        if (srv.conns() > peak) peak = srv.conns();
    }
    printf("  (concurrent: %ld steps, peak %d connections)\n", steps, peak);
    CHECK(wait_child(pid) == 0);
    CHECK(srv.stopped());
    CHECK(peak >= 50);
    free(st.big);
}

// ---- our client against canned responses -------------------------------

static void read_head(tcp::Conn* c) {
    fastring s;
    char b;
    while (co_http_head_end(s.data(), s.size(), 0) == 0) {
        if (c->recv(&b, 1, 2000) != 1) break;
        s.append_char(b);
    }
}

static void canned_client(int port) {
    char url[64];
    snprintf(url, sizeof(url), "127.0.0.1:%d", port);
    http::Client cli;
    CHECK(cli.open(url));

    CHECK(cli.get("/a"));                    // chunked, with trailer
    CHECK(cli.status() == 200);
    CHECK_STR(cli.body(), "hello world");
    CHECK(cli.get("/b"));                    // 100 Continue, then the real one
    CHECK(cli.status() == 201);
    CHECK_STR(cli.body(), "ok");
    CHECK(cli.get("/c"));                    // HTTP/1.0, body ends at close
    CHECK_STR(cli.body(), "until-close");

    CHECK(cli.get("/d"));
    CHECK_STR(cli.body(), "a");
    CHECK(cli.get("/e"));                    // server dropped it: one retry
    CHECK_STR(cli.body(), "b");

    CHECK(!cli.get("/f"));                   // conflicting Content-Length
    CHECK(strstr(cli.error(), "malformed") != 0);
    CHECK(cli.status() == 0);
    CHECK(!cli.get("/g"));                   // truncated body
    cli.set_timeout(200);
    CHECK(!cli.get("/h"));                   // never answered
    CHECK_STR(cli.error(), "timed out");

    CHECK(!cli.open("https://127.0.0.1"));
    CHECK(strstr(cli.error(), "TLS") != 0);
    CHECK(!cli.open("ftp://127.0.0.1"));
    CHECK(!cli.get("/"));
}

static void test_client() {
    tcp::Server s;
    CHECK(s.start("127.0.0.1", 0, 16));
    const int port = s.port();

    fflush(stdout);
    const int pid = fork();
    if (pid == 0) {
        const int before = g_fails;
        s.close();
        canned_client(port);
        child_exit(before);
    }

    tcp::Conn a;
    CHECK(s.accept(&a, 2000));
    read_head(&a);
    a.send_cstr("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "5\r\nhello\r\n6\r\n world\r\n0\r\nX-T: 1\r\n\r\n", 2000);
    read_head(&a);
    a.send_cstr("HTTP/1.1 100 Continue\r\n\r\n"
                "HTTP/1.1 201 Created\r\nContent-Length: 2\r\n\r\nok", 2000);
    read_head(&a);
    a.send_cstr("HTTP/1.0 200 OK\r\n\r\nuntil-close", 2000);
    a.close();

    tcp::Conn b;
    CHECK(s.accept(&b, 2000));
    read_head(&b);
    b.send_cstr("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\na", 2000);
    b.close();

    tcp::Conn c;
    CHECK(s.accept(&c, 2000));
    read_head(&c);
    c.send_cstr("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nb", 2000);
    read_head(&c);
    c.send_cstr("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab", 2000);
    c.close();

    tcp::Conn d;
    CHECK(s.accept(&d, 2000));
    read_head(&d);
    d.send_cstr("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc", 2000);
    d.close();

    tcp::Conn e;
    CHECK(s.accept(&e, 2000));
    read_head(&e);
    char buf[8];
    CHECK(e.recv(buf, sizeof(buf), 3000) == 0); // the client gives up and closes

    CHECK(wait_child(pid) == 0);
}

int main() {
    test_parser();
    test_chunked();
    test_url();
    test_server();
    test_concurrent();
    test_client();
    return test_report("http");
}
