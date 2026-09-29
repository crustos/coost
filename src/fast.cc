#include "co/fast.h"
#include <string.h>

namespace fast {

static const char kHex[] = "0123456789abcdef";

static const char kDigits[] =
    "00010203040506070809" "10111213141516171819"
    "20212223242526272829" "30313233343536373839"
    "40414243444546474849" "50515253545556575859"
    "60616263646566676869" "70717273747576777879"
    "80818283848586878889" "90919293949596979899";

int u64toh(uint64 v, char* buf) {
    char tmp[16];
    int n = 0;
    do {
        tmp[n++] = kHex[v & 15];
        v >>= 4;
    } while (v);
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < n; ++i) buf[2 + i] = tmp[n - 1 - i];
    return n + 2;
}

int u32toh(uint32 v, char* buf) {
    return u64toh((uint64)v, buf);
}

int u64toa(uint64 v, char* buf) {
    char tmp[20];
    int n = 20;
    while (v >= 100) {
        const unsigned i = (unsigned)(v % 100) * 2;
        v /= 100;
        tmp[--n] = kDigits[i + 1];
        tmp[--n] = kDigits[i];
    }
    if (v >= 10) {
        const unsigned i = (unsigned)v * 2;
        tmp[--n] = kDigits[i + 1];
        tmp[--n] = kDigits[i];
    } else {
        tmp[--n] = (char)('0' + v);
    }
    memcpy(buf, tmp + n, 20 - n);
    return 20 - n;
}

int u32toa(uint32 v, char* buf) {
    return u64toa((uint64)v, buf);
}

} // fast
