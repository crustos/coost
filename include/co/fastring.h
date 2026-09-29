#pragma once

// fastring: a growable byte string that doubles as an output stream.
//
// Written in the Crust C++ subset, so the API follows its rules:
//   - overloads differ by argument count only, so type variants carry
//     their type in the name (append_cstr, append_int, find_char, ...);
//   - no reference returns except operator[]; appends return void;
//   - no default arguments.

#include "def.h"
#include "mem.h"
#include "fast.h"
#include <string.h>

namespace str {
char* memrchr(const char* s, char c, size_t n);
char* memmem(const char* s, size_t n, const char* p, size_t m);
char* memimem(const char* s, size_t n, const char* p, size_t m);
char* memrmem(const char* s, size_t n, const char* p, size_t m);
bool match(const char* s, size_t n, const char* p, size_t m);

static inline int memcmp(const char* s, size_t n, const char* p, size_t m) {
    const int i = ::memcmp(s, p, n < m ? n : m);
    return i != 0 ? i : (n < m ? -1 : n != m);
}
} // str

class fastring {
  public:
    static const size_t npos = (size_t)-1;

    fastring() {
        _cap = 0;
        _size = 0;
        _p = 0;
    }

    // empty string with @cap bytes reserved
    fastring(size_t cap) {
        _cap = cap;
        _size = 0;
        _p = cap > 0 ? (char*) co::alloc(cap) : 0;
    }

    // copy of the @n bytes at @s
    fastring(const void* s, size_t n) {
        _cap = n + 1;
        _size = n;
        _p = (char*) co::alloc(_cap);
        if (n > 0) memcpy(_p, s, n);
    }

    fastring(const fastring& s) {
        _size = s._size;
        _cap = s._size + 1;
        _p = (char*) co::alloc(_cap);
        if (_size > 0) memcpy(_p, s._p, _size);
    }

    fastring(fastring&& s) {
        _cap = s._cap;
        _size = s._size;
        _p = s._p;
        s._p = 0;
        s._cap = 0;
        s._size = 0;
    }

    ~fastring() {
        this->reset();
    }

    void operator=(const fastring& s) {
        if (&s != this) this->assign(s._p, s._size);
    }

    void operator=(fastring&& s) {
        if (&s != this) {
            this->reset();
            _cap = s._cap;
            _size = s._size;
            _p = s._p;
            s._p = 0;
            s._cap = 0;
            s._size = 0;
        }
    }

    static fastring from_cstr(const char* s) {
        fastring r(s, strlen(s));
        return r;
    }

    // @n copies of @c
    static fastring repeat(size_t n, char c) {
        fastring r(n + 1);
        r.append_chars(n, c);
        return r;
    }

    // ---- storage ----------------------------------------------------

    char* data() const { return _p; }
    size_t size() const { return _size; }
    bool empty() const { return _size == 0; }
    size_t capacity() const { return _cap; }
    void clear() { _size = 0; }

    // always NUL-terminated: every allocation keeps one spare byte
    const char* c_str() const {
        if (_p == 0) return "";
        _p[_size] = '\0';
        return _p;
    }

    char& operator[](size_t i) { return _p[i]; }
    char back() const { return _p[_size - 1]; }
    char front() const { return _p[0]; }

    // resize only, the expanded memory is not initialized
    void resize(size_t n) {
        this->reserve(n + 1);
        _size = n;
    }

    // resize and fill the expanded memory with @c
    void resize(size_t n, char c) {
        this->reserve(n + 1);
        if (_size < n) memset(_p + _size, c, n - _size);
        _size = n;
    }

    void reserve(size_t n) {
        if (_cap < n) {
            _p = (char*) co::realloc(_p, _cap, n);
            _cap = n;
        }
    }

    // free the memory
    void reset() {
        if (_p) {
            co::free(_p, _cap);
            _p = 0;
            _cap = 0;
            _size = 0;
        }
    }

    // make room to append @n more bytes (plus a trailing '\0')
    void ensure(size_t n) {
        if (_cap < _size + n + 1) {
            const size_t cap = _cap;
            _cap += (_cap >> 1) + n + 1;
            _p = (char*) co::realloc(_p, cap, _cap);
        }
    }

    void swap(fastring& s) {
        char* const p = _p;
        const size_t cap = _cap;
        const size_t size = _size;
        _p = s._p;
        _cap = s._cap;
        _size = s._size;
        s._p = p;
        s._cap = cap;
        s._size = size;
    }

    // release unused capacity
    void shrink() {
        if (_p && _size + 1 < _cap) {
            _p = (char*) co::realloc(_p, _cap, _size + 1);
            _cap = _size + 1;
        }
    }

    // ---- assign -----------------------------------------------------

    void assign(const void* s, size_t n) {
        const char* const q = (const char*) s;
        if (_p <= q && q < _p + _size) {
            if (q != _p) memmove(_p, q, n);
            _size = n;
            return;
        }
        _size = 0;
        this->append(s, n);
    }

    void assign_cstr(const char* s) { this->assign(s, strlen(s)); }

    // ---- append -----------------------------------------------------

    void append(const void* s, size_t n) {
        const char* const q = (const char*) s;
        if (n == 0) return;
        if (_p <= q && q < _p + _size) {
            const size_t pos = q - _p;
            this->ensure(n);
            memcpy(_p + _size, _p + pos, n);
        } else {
            this->ensure(n);
            memcpy(_p + _size, q, n);
        }
        _size += n;
    }

    void append_cstr(const char* s) { this->append(s, strlen(s)); }
    void append_str(const fastring& s) { this->append(s._p, s._size); }

    void append_char(char c) {
        this->ensure(1);
        _p[_size++] = c;
    }

    // append @n copies of @c
    void append_chars(size_t n, char c) {
        this->ensure(n);
        memset(_p + _size, c, n);
        _size += n;
    }

    void push_back(char c) { this->append_char(c); }
    char pop_back() { return _p[--_size]; }

    // ---- formatted append (text) --------------------------------------

    void append_bool(bool v) {
        if (v) {
            this->append("true", 4);
        } else {
            this->append("false", 5);
        }
    }

    void append_int(int64 v) {
        this->ensure(24);
        _size += fast::i64toa(v, _p + _size);
    }

    void append_uint(uint64 v) {
        this->ensure(24);
        _size += fast::u64toa(v, _p + _size);
    }

    // at most @mdp decimal places; 324 prints every significant digit
    void append_double(double v, int mdp) {
        this->ensure(mdp + 32);
        _size += fast::dtoa(v, _p + _size, mdp);
    }

    void append_hex(uint64 v) {
        this->ensure(24);
        _size += fast::u64toh(v, _p + _size);
    }

    void append_ptr(const void* p) {
        this->ensure(24);
        _size += fast::ptoh(p, _p + _size);
    }

    // ---- binary append (raw bytes, host order) ------------------------

    void append_u16(uint16 v) { this->append(&v, sizeof(v)); }
    void append_u32(uint32 v) { this->append(&v, sizeof(v)); }
    void append_u64(uint64 v) { this->append(&v, sizeof(v)); }

    // ---- compare ----------------------------------------------------

    int compare(const char* s, size_t n) const {
        return str::memcmp(_p, _size, s, n);
    }

    int compare_cstr(const char* s) const { return this->compare(s, strlen(s)); }
    int compare_str(const fastring& s) const { return this->compare(s._p, s._size); }

    bool eq(const char* s, size_t n) const {
        return _size == n && (n == 0 || ::memcmp(_p, s, n) == 0);
    }

    bool eq_cstr(const char* s) const { return this->eq(s, strlen(s)); }

    bool eq_str(const fastring& s) const { return this->eq(s._p, s._size); }

    bool starts_with(const char* s, size_t n) const {
        return n == 0 || (n <= _size && ::memcmp(_p, s, n) == 0);
    }

    bool starts_with_cstr(const char* s) const { return this->starts_with(s, strlen(s)); }
    bool starts_with_char(char c) const { return _size > 0 && _p[0] == c; }

    bool ends_with(const char* s, size_t n) const {
        return n == 0 || (n <= _size && ::memcmp(_p + _size - n, s, n) == 0);
    }

    bool ends_with_cstr(const char* s) const { return this->ends_with(s, strlen(s)); }
    bool ends_with_char(char c) const { return _size > 0 && _p[_size - 1] == c; }

    bool contains_char(char c) const { return this->find_char(c) != npos; }
    bool contains_cstr(const char* s) const { return this->find_cstr(s) != npos; }

    // glob match: '*' matches any run, '?' any single character
    bool match(const char* pattern) const {
        return str::match(_p, _size, pattern, strlen(pattern));
    }

    // ---- find (all return npos when not found) ------------------------

    size_t find_char(char c) const {
        if (_size == 0) return npos;
        const char* const p = (const char*) memchr(_p, c, _size);
        return p ? (size_t)(p - _p) : npos;
    }

    size_t find_char_from(char c, size_t pos) const {
        if (pos >= _size) return npos;
        const char* const p = (const char*) memchr(_p + pos, c, _size - pos);
        return p ? (size_t)(p - _p) : npos;
    }

    // find the @n bytes at @s, starting at @pos
    size_t find(const char* s, size_t pos, size_t n) const {
        if (pos > _size) return npos;
        const char* const p = str::memmem(_p + pos, _size - pos, s, n);
        return p ? (size_t)(p - _p) : npos;
    }

    size_t find_cstr(const char* s) const { return this->find(s, 0, strlen(s)); }
    size_t find_cstr_from(const char* s, size_t pos) const { return this->find(s, pos, strlen(s)); }

    // case-insensitive find
    size_t ifind_cstr(const char* s, size_t pos) const {
        if (pos > _size) return npos;
        const char* const p = str::memimem(_p + pos, _size - pos, s, strlen(s));
        return p ? (size_t)(p - _p) : npos;
    }

    size_t rfind_char(char c) const {
        const char* const p = str::memrchr(_p, c, _size);
        return p ? (size_t)(p - _p) : npos;
    }

    size_t rfind_cstr(const char* s) const {
        const size_t n = strlen(s);
        if (n == 0) return _size;
        const char* const p = str::memrmem(_p, _size, s, n);
        return p ? (size_t)(p - _p) : npos;
    }

    // character-set searches: @s is the set, @pos where to start
    // (for the *_last_* forms, where to start searching backwards)
    size_t find_first_of(const char* s, size_t pos) const;
    size_t find_first_not_of(const char* s, size_t pos) const;
    size_t find_last_of(const char* s, size_t pos) const;
    size_t find_last_not_of(const char* s, size_t pos) const;

    // ---- modify -----------------------------------------------------

    fastring substr(size_t pos) const {
        if (pos >= _size) {
            fastring e;
            return e;
        }
        fastring s(_p + pos, _size - pos);
        return s;
    }

    fastring substr(size_t pos, size_t len) const {
        if (pos >= _size) {
            fastring e;
            return e;
        }
        const size_t n = _size - pos;
        fastring s(_p + pos, len < n ? len : n);
        return s;
    }

    // trim characters in @chars; @d is 'l' (left), 'r' (right) or 'b' (both)
    void trim_cstr(const char* chars, char d);

    // trim whitespace on both sides
    void trim() { this->trim_cstr(" \t\r\n", 'b'); }

    void trim_char(char c, char d) {
        char s[2];
        s[0] = c;
        s[1] = '\0';
        this->trim_cstr(s, d);
    }

    // remove @n characters; @d as for trim_cstr
    void trim_n(size_t n, char d);

    void remove_prefix_cstr(const char* s) {
        const size_t n = strlen(s);
        if (this->starts_with(s, n)) this->trim_n(n, 'l');
    }

    void remove_suffix_cstr(const char* s) {
        const size_t n = strlen(s);
        if (this->ends_with(s, n)) _size -= n;
    }

    // replace @sub with @to, at most @maxreplace times (0 for all)
    void replace(const char* sub, size_t n, const char* to, size_t m, size_t maxreplace);

    void replace_cstr(const char* sub, const char* to, size_t maxreplace) {
        this->replace(sub, strlen(sub), to, strlen(to), maxreplace);
    }

    void tolower();
    void toupper();

    fastring lower() const {
        fastring s(_p, _size);
        s.tolower();
        return s;
    }

    fastring upper() const {
        fastring s(_p, _size);
        s.toupper();
        return s;
    }

  private:
    size_t _cap;
    size_t _size;
    char* _p;
};

// fastream was a separate move-only stream class upstream; here the one
// class serves both purposes.
typedef fastring fastream;
