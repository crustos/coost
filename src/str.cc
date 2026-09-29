#include "co/str.h"
#include <errno.h>
#include <stdlib.h>

namespace str {

fastring replace(const char* s, size_t n, const char* sub, const char* to, size_t t) {
    fastring x(s, n);
    x.replace_cstr(sub, to, t);
    return x;
}

co::vector<fastring> split(const char* s, size_t n, char c, size_t t) {
    co::vector<fastring> v;
    const char* const end = s + n;
    const char* p = (const char*) memchr(s, c, n);
    while (p) {
        fastring e(s, p - s);
        v.push_back(e);
        s = p + 1;
        if (v.size() == t) break;
        p = (const char*) memchr(s, c, end - s);
    }
    if (s < end) {
        fastring e(s, end - s);
        v.push_back(e);
    }
    return v;
}

co::vector<fastring> split_cstr(const char* s, size_t n, const char* sep, size_t t) {
    co::vector<fastring> v;
    const size_t m = strlen(sep);
    if (m == 0) return v;
    const char* const end = s + n;
    const char* p = str::memmem(s, n, sep, m);
    while (p) {
        fastring e(s, p - s);
        v.push_back(e);
        s = p + m;
        if (v.size() == t) break;
        p = str::memmem(s, end - s, sep, m);
    }
    if (s < end) {
        fastring e(s, end - s);
        v.push_back(e);
    }
    return v;
}

bool to_bool(const char* s) {
    errno = 0;
    if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0) return false;
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) return true;
    errno = EINVAL;
    return false;
}

// bit shift for a size suffix: k, m, g, t, p (powers of 1024)
static int str_suffix_shift(char c) {
    switch (c) {
      case 'k': case 'K': return 10;
      case 'm': case 'M': return 20;
      case 'g': case 'G': return 30;
      case 't': case 'T': return 40;
      case 'p': case 'P': return 50;
      default: return 0;
    }
}

int64 to_int64(const char* s) {
    errno = 0;
    if (!*s) return 0;

    char* end = 0;
    const int64 x = strtoll(s, &end, 0);
    if (errno != 0) return 0;

    const size_t n = strlen(s);
    if (end == s + n) return x;

    if (end == s + n - 1) {
        const int shift = str_suffix_shift(s[n - 1]);
        if (shift != 0) {
            if (x < (MIN_INT64 >> shift) || x > (MAX_INT64 >> shift)) {
                errno = ERANGE;
                return 0;
            }
            return (int64)((uint64)x << shift);
        }
    }
    errno = EINVAL;
    return 0;
}

uint64 to_uint64(const char* s) {
    errno = 0;
    if (!*s) return 0;

    char* end = 0;
    const uint64 x = strtoull(s, &end, 0);
    if (errno != 0) return 0;

    const size_t n = strlen(s);
    if (end == s + n) return x;

    if (end == s + n - 1) {
        const int shift = str_suffix_shift(s[n - 1]);
        if (shift != 0) {
            if (x > (MAX_UINT64 >> shift)) {
                errno = ERANGE;
                return 0;
            }
            return x << shift;
        }
    }
    errno = EINVAL;
    return 0;
}

int32 to_int32(const char* s) {
    const int64 x = to_int64(s);
    if (x > MAX_INT32 || x < MIN_INT32) {
        errno = ERANGE;
        return 0;
    }
    return (int32)x;
}

uint32 to_uint32(const char* s) {
    const uint64 x = to_uint64(s);
    if (x > MAX_UINT32) {
        errno = ERANGE;
        return 0;
    }
    return (uint32)x;
}

double to_double(const char* s) {
    errno = 0;
    char* end = 0;
    const double x = strtod(s, &end);
    if (errno != 0) return 0;
    if (end == s + strlen(s)) return x;
    errno = EINVAL;
    return 0;
}

} // str
