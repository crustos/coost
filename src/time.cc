#include "co/time.h"
#include <errno.h>
#include <time.h>
#include <sys/time.h>

/* The libc calls live outside the namespaces: cpprust's namespace
   flattening would otherwise rename `struct timespec` to `now_timespec`. */

static int64 time_mono_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64)t.tv_sec * 1000000000 + t.tv_nsec;
}

static int64 time_epoch_us() {
    struct timeval t;
    gettimeofday(&t, 0);
    return (int64)t.tv_sec * 1000000 + t.tv_usec;
}

static size_t time_format(char* buf, size_t n, const char* fm) {
    const time_t x = time(0);
    struct tm t;
    localtime_r(&x, &t);
    return strftime(buf, n, fm, &t);
}

static void time_sleep(uint32 sec, uint32 nsec) {
    struct timespec ts;
    ts.tv_sec = sec;
    ts.tv_nsec = nsec;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

namespace now {

int64 ns() { return time_mono_ns(); }
int64 us() { return time_mono_ns() / 1000; }
int64 ms() { return time_mono_ns() / 1000000; }

fastring str(const char* fm) {
    char buf[256];
    const size_t r = time_format(buf, sizeof(buf), fm);
    fastring s(buf, r);
    return s;
}

} // now

namespace epoch {

int64 us() { return time_epoch_us(); }
int64 ms() { return time_epoch_us() / 1000; }

} // epoch

namespace co {

void sleep_ms(uint32 n) { time_sleep(n / 1000, (n % 1000) * 1000000); }
void sleep_sec(uint32 n) { time_sleep(n, 0); }

} // co
