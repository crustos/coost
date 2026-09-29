#include "co/time.h"
#include <errno.h>
#include <time.h>
#include <sys/time.h>

namespace now {

int64 ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64)t.tv_sec * 1000000000 + t.tv_nsec;
}

int64 us() { return now::ns() / 1000; }
int64 ms() { return now::ns() / 1000000; }

fastring str(const char* fm) {
    const time_t x = time(0);
    struct tm t;
    localtime_r(&x, &t);
    char buf[256];
    const size_t r = strftime(buf, sizeof(buf), fm, &t);
    fastring s(buf, r);
    return s;
}

} // now

namespace epoch {

int64 us() {
    struct timeval t;
    gettimeofday(&t, 0);
    return (int64)t.tv_sec * 1000000 + t.tv_usec;
}

int64 ms() { return epoch::us() / 1000; }

} // epoch

namespace co {

void sleep_ms(uint32 n) {
    struct timespec ts;
    ts.tv_sec = n / 1000;
    ts.tv_nsec = (long)(n % 1000) * 1000000;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

void sleep_sec(uint32 n) {
    struct timespec ts;
    ts.tv_sec = n;
    ts.tv_nsec = 0;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

} // co
