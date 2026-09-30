#include "test.h"
#include "co/tcp.h"
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

// Parent is the server, the fork()ed child the client, as in crust's
// examples/rpython2c/net/socket_echo.py. The child reports through its
// exit status.

static int wait_child(int pid) {
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 99;
}

static void test_echo() {
    tcp::Server s;
    CHECK(s.start("127.0.0.1", 0, 16));
    const int port = s.port();
    CHECK(port > 0);

    fflush(stdout);
    const int pid = fork();
    if (pid == 0) {
        s.close();
        tcp::Conn c;
        char buf[16];
        int ok = c.connect("127.0.0.1", port, 2000) ? 1 : 0;
        if (ok) ok = c.send_cstr("hello", 2000) == 5;
        if (ok) ok = c.recvn(buf, 5, 2000) == 5 && memcmp(buf, "HELLO", 5) == 0;
        fastring peer = c.peer();
        if (ok) ok = peer.starts_with_cstr("127.0.0.1:");
        c.close();
        _exit(ok ? 0 : 1);
    }

    tcp::Conn c;
    char buf[16];
    CHECK(s.accept(&c, 2000));
    CHECK(c.is_open());
    CHECK(c.recvn(buf, 5, 2000) == 5);
    for (int i = 0; i < 5; ++i) buf[i] = (char)(buf[i] - 32);
    CHECK(c.send(buf, 5, 2000) == 5);
    CHECK(c.recv(buf, 16, 2000) == 0); // the child closed
    CHECK(wait_child(pid) == 0);
}

static void test_timeouts() {
    tcp::Server s;
    CHECK(s.start("127.0.0.1", 0, 16));
    const int port = s.port();

    tcp::Conn c;
    errno = 0;
    CHECK(!s.accept(&c, 50));
    CHECK(errno == ETIMEDOUT);

    // connected, but the peer never writes
    CHECK(c.connect("127.0.0.1", port, 2000));
    tcp::Conn a;
    CHECK(s.accept(&a, 2000));
    char buf[4];
    errno = 0;
    CHECK(c.recv(buf, 4, 50) == -1);
    CHECK(errno == ETIMEDOUT);

    // nothing listens once the server is closed
    a.close();
    s.close();
    tcp::Conn d;
    CHECK(!d.connect("127.0.0.1", port, 2000));
    CHECK(!d.is_open());
    CHECK(!d.connect("no-such-host.invalid", 80, 2000));
}

static void test_large() {
    // bigger than any socket buffer, so send and recvn both have to wait
    const int n = 4 << 20;
    tcp::Server s;
    CHECK(s.start("127.0.0.1", 0, 16));
    const int port = s.port();

    fflush(stdout);
    const int pid = fork();
    if (pid == 0) {
        s.close();
        tcp::Conn c;
        char* p = (char*)malloc(n);
        for (int i = 0; i < n; ++i) p[i] = (char)(i * 7);
        int ok = c.connect("127.0.0.1", port, 2000) ? 1 : 0;
        if (ok) ok = c.send(p, n, 5000) == n;
        free(p);
        c.close();
        _exit(ok ? 0 : 1);
    }

    tcp::Conn c;
    CHECK(s.accept(&c, 2000));
    char* p = (char*)malloc(n);
    CHECK(c.recvn(p, n, 5000) == n);
    int bad = 0;
    for (int i = 0; i < n; ++i) {
        if (p[i] != (char)(i * 7)) bad++;
    }
    CHECK(bad == 0);
    free(p);
    CHECK(wait_child(pid) == 0);
}

int main() {
    test_echo();
    test_timeouts();
    test_large();
    return test_report("tcp");
}
