#include "co/path.h"

namespace path {

fastring clean(const char* s, size_t n) {
    if (n == 0) {
        fastring d(".", 1);
        return d;
    }

    fastring r(s, n);
    const bool rooted = s[0] == '/';
    const size_t beg = rooted;
    size_t p = rooted;      // write index into r
    size_t dotdot = rooted; // index where .. must stop

    for (size_t i = p; i < n;) {
        if (s[i] == '/' || (s[i] == '.' && (i + 1 == n || s[i + 1] == '/'))) {
            // empty or . element
            ++i;
        } else if (s[i] == '.' && i + 1 < n && s[i + 1] == '.' && (i + 2 == n || s[i + 2] == '/')) {
            // .. element: remove to last /
            i += 2;
            if (p > dotdot) {
                --p;
                while (p > dotdot && r[p] != '/') --p;
            } else if (!rooted) {
                // cannot backtrack, but not rooted, so append .. element
                if (p > 0) r[p++] = '/';
                r[p++] = '.';
                r[p++] = '.';
                dotdot = p;
            }
        } else {
            // real path element, add slash if needed
            if ((rooted && p != beg) || (!rooted && p != 0)) r[p++] = '/';
            for (; i < n && s[i] != '/'; ++i) r[p++] = s[i];
        }
    }

    if (p == 0) {
        fastring d(".", 1);
        return d;
    }
    r.resize(p);
    return r;
}

fastring join(const char* a, const char* b) {
    const size_t n = strlen(a);
    const size_t m = strlen(b);
    fastring v(n + m + 2);
    v.append(a, n);
    if (n > 0 && m > 0) v.append_char('/');
    v.append(b, m);
    if (v.empty()) return v;
    fastring c = clean(v.data(), v.size());
    return c;
}

void split(const char* s, size_t n, fastring* dir, fastring* file) {
    const char* p = str::memrchr(s, '/', n);
    const size_t m = p ? (size_t)(p + 1 - s) : 0;
    dir->assign(s, m);
    file->assign(s + m, n - m);
}

fastring dir(const char* s, size_t n) {
    const char* p = str::memrchr(s, '/', n);
    if (p == 0) {
        fastring d(".", 1);
        return d;
    }
    fastring c = clean(s, p + 1 - s);
    return c;
}

fastring base(const char* s, size_t n) {
    if (n == 0) {
        fastring d(".", 1);
        return d;
    }

    size_t p = n;
    while (p > 0 && s[p - 1] == '/') --p;
    if (p == 0) {
        fastring d("/", 1);
        return d;
    }

    const size_t e = p;
    while (p > 0 && s[p - 1] != '/') --p;
    fastring b(s + p, e - p);
    return b;
}

fastring ext(const char* s, size_t n) {
    const char* const e = s + n;
    for (const char* p = e; p != s && *--p != '/';) {
        if (*p == '.') {
            fastring x(p, e - p);
            return x;
        }
    }
    fastring x;
    return x;
}

} // path
