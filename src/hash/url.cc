#include "co/hash/url.h"


static const char url_tb[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static inline bool url_unencoded(uint8 c) {
    return url_tb[c];
}

static inline int url_hex2int(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    return -1;
}

fastring url_encode(const void* s, size_t n) {
    fastring dst(n + 32);
    const char* p = (const char*)s;

    char c;
    for (size_t i = 0; i < n; ++i) {
        c = p[i];
        if (url_unencoded((uint8)c)) {
            dst.append_char(c);
            continue;
        }
        dst.append_char('%');
        dst.append_char("0123456789ABCDEF"[(uint8)c >> 4]);
        dst.append_char("0123456789ABCDEF"[(uint8)c & 0x0F]);
    }

    return dst;
}

fastring url_decode(const void* s, size_t n) {
    fastring dst(n);
    const char* p = (const char*)s;

    char c;
    for (size_t i = 0; i < n; ++i) {
        c = p[i];
        if (c != '%') {
            dst.append_char(c);
            continue;
        }

        if (i + 2 >= n) { fastring e; return e; }  // invalid encode
        const int h4 = url_hex2int(p[i + 1]);
        const int l4 = url_hex2int(p[i + 2]);
        if (h4 < 0 || l4 < 0) { fastring e; return e; }  // invalid encode

        dst.append_char((char)((h4 << 4) | l4));
        i += 2;
    }

    return dst;
}
