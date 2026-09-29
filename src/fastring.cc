#include "co/fastring.h"
#include <ctype.h>

namespace str {

char* memrchr(const char* s, char c, size_t n) {
    for (size_t i = n; i > 0; --i) {
        if (s[i - 1] == c) return (char*)(s + i - 1);
    }
    return 0;
}

char* memmem(const char* s, size_t n, const char* p, size_t m) {
    if (m == 0) return (char*)s;
    if (n < m) return 0;
    const char* const e = s + (n - m);
    for (const char* q = s; q <= e; ++q) {
        q = (const char*) memchr(q, *p, (size_t)(e - q) + 1);
        if (q == 0) return 0;
        if (::memcmp(q, p, m) == 0) return (char*)q;
    }
    return 0;
}

char* memimem(const char* s, size_t n, const char* p, size_t m) {
    if (m == 0) return (char*)s;
    if (n < m) return 0;
    for (size_t i = 0; i + m <= n; ++i) {
        size_t j = 0;
        while (j < m && ::tolower((unsigned char)s[i + j]) == ::tolower((unsigned char)p[j])) ++j;
        if (j == m) return (char*)(s + i);
    }
    return 0;
}

char* memrmem(const char* s, size_t n, const char* p, size_t m) {
    if (m == 0) return (char*)(s + n);
    if (n < m) return 0;
    for (size_t i = n - m + 1; i > 0; --i) {
        if (::memcmp(s + i - 1, p, m) == 0) return (char*)(s + i - 1);
    }
    return 0;
}

bool match(const char* s, size_t n, const char* p, size_t m) {
    char c;
    while (n > 0 && m > 0 && (c = p[m - 1]) != '*') {
        if (c != s[n - 1] && c != '?') return false;
        --n;
        --m;
    }
    if (m == 0) return n == 0;

    size_t si = 0, pi = 0, sl = (size_t)-1, pl = (size_t)-1;
    while (si < n && pi < m) {
        c = p[pi];
        if (c == '*') {
            sl = si;
            pl = ++pi;
            continue;
        }
        if (c == s[si] || c == '?') {
            ++si;
            ++pi;
            continue;
        }
        if (sl != (size_t)-1 && sl + 1 < n) {
            si = ++sl;
            pi = pl;
            continue;
        }
        return false;
    }

    while (pi < m) {
        if (p[pi++] != '*') return false;
    }
    return true;
}

} // str

void fastring::trim_cstr(const char* chars, char d) {
    if (_size == 0 || chars == 0 || *chars == '\0') return;

    unsigned char bs[256];
    memset(bs, 0, sizeof(bs));
    for (const unsigned char* s = (const unsigned char*)chars; *s; ++s) bs[*s] = 1;

    const unsigned char* const p = (const unsigned char*)_p;
    if (d != 'l' && d != 'L') {
        size_t e = _size;
        while (e > 0 && bs[p[e - 1]]) --e;
        _size = e;
    }
    if (d != 'r' && d != 'R') {
        size_t b = 0;
        while (b < _size && bs[p[b]]) ++b;
        if (b != 0) {
            _size -= b;
            if (_size != 0) memmove(_p, _p + b, _size);
        }
    }
}

void fastring::trim_n(size_t n, char d) {
    if (_size == 0) return;
    if (d == 'r' || d == 'R') {
        _size = n < _size ? _size - n : 0;
    } else if (d == 'l' || d == 'L') {
        if (n < _size) {
            _size -= n;
            memmove(_p, _p + n, _size);
        } else {
            _size = 0;
        }
    } else {
        if (n * 2 < _size) {
            _size -= n * 2;
            memmove(_p, _p + n, _size);
        } else {
            _size = 0;
        }
    }
}

void fastring::replace(const char* sub, size_t n, const char* to, size_t m, size_t maxreplace) {
    if (_size == 0 || n == 0) return;

    const char* from = _p;
    const char* p = str::memmem(_p, _size, sub, n);
    if (!p) return;

    const char* const e = _p + _size;
    fastring s(_size + 1);
    while (p) {
        s.append(from, p - from);
        s.append(to, m);
        from = p + n;
        if (maxreplace && --maxreplace == 0) break;
        p = str::memmem(from, e - from, sub, n);
    }
    if (from < e) s.append(from, e - from);
    this->swap(s);
}

void fastring::toupper() {
    for (size_t i = 0; i < _size; ++i) {
        if ('a' <= _p[i] && _p[i] <= 'z') _p[i] ^= 32;
    }
}

void fastring::tolower() {
    for (size_t i = 0; i < _size; ++i) {
        if ('A' <= _p[i] && _p[i] <= 'Z') _p[i] ^= 32;
    }
}

size_t fastring::find_first_of(const char* s, size_t pos) const {
    unsigned char bs[256];
    memset(bs, 0, sizeof(bs));
    for (const unsigned char* q = (const unsigned char*)s; *q; ++q) bs[*q] = 1;
    for (size_t i = pos; i < _size; ++i) {
        if (bs[(unsigned char)_p[i]]) return i;
    }
    return npos;
}

size_t fastring::find_first_not_of(const char* s, size_t pos) const {
    unsigned char bs[256];
    memset(bs, 0, sizeof(bs));
    for (const unsigned char* q = (const unsigned char*)s; *q; ++q) bs[*q] = 1;
    for (size_t i = pos; i < _size; ++i) {
        if (!bs[(unsigned char)_p[i]]) return i;
    }
    return npos;
}

size_t fastring::find_last_of(const char* s, size_t pos) const {
    unsigned char bs[256];
    memset(bs, 0, sizeof(bs));
    for (const unsigned char* q = (const unsigned char*)s; *q; ++q) bs[*q] = 1;
    for (size_t i = (pos >= _size ? _size : pos + 1); i > 0; --i) {
        if (bs[(unsigned char)_p[i - 1]]) return i - 1;
    }
    return npos;
}

size_t fastring::find_last_not_of(const char* s, size_t pos) const {
    unsigned char bs[256];
    memset(bs, 0, sizeof(bs));
    for (const unsigned char* q = (const unsigned char*)s; *q; ++q) bs[*q] = 1;
    for (size_t i = (pos >= _size ? _size : pos + 1); i > 0; --i) {
        if (!bs[(unsigned char)_p[i - 1]]) return i - 1;
    }
    return npos;
}
