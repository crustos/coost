#pragma once

// co::vector<T>: a minimal growable array.
//
// Elements are copied in with T's copy constructor and destroyed with its
// destructor. Under the Crust subset that is spelled with the element
// builtins (__cpp_copy, __cpp_drop, __cpp_ref), which resolve per
// instantiation to the class functions or to plain assignment for a
// scalar. A C++ compiler does not know them, so they get C++ meanings here.

#include "def.h"
#include "mem.h"
#include <string.h>

#ifndef CO_CRUST
#include <new>
#define __cpp_ref(T) const T&
#define __cpp_copy(T, dst, src) (new (&(dst)) T(src))
#define __cpp_drop(T, x) ((x).~T())
#endif

namespace co {

template<typename T>
class vector {
  public:
    vector() {
        _p = 0;
        _size = 0;
        _cap = 0;
    }

    ~vector() {
        this->clear();
        if (_p) co::free(_p, _cap * sizeof(T));
    }

    size_t size() const { return _size; }
    bool empty() const { return _size == 0; }
    size_t capacity() const { return _cap; }
    T* data() const { return _p; }

    T& operator[](size_t i) { return _p[i]; }

    void reserve(size_t n) {
        if (_cap < n) {
            _p = (T*) co::realloc(_p, _cap * sizeof(T), n * sizeof(T));
            _cap = n;
        }
    }

    void push_back(__cpp_ref(T) v) {
        if (_size == _cap) this->reserve(_cap ? _cap * 2 : 8);
        __cpp_copy(T, _p[_size], v);
        _size++;
    }

    // destroy the last element
    void pop_back() {
        _size--;
        __cpp_drop(T, _p[_size]);
    }

    void clear() {
        for (size_t i = 0; i < _size; ++i) {
            __cpp_drop(T, _p[i]);
        }
        _size = 0;
    }

  private:
    T* _p;
    size_t _size;
    size_t _cap;
};

} // co
