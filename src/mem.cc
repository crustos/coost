#include "co/mem.h"

namespace co {

void* alloc(size_t n) {
    return ::malloc(n);
}

void* alloc_aligned(size_t n, size_t align) {
    void* p = 0;
    if (align < sizeof(void*)) align = sizeof(void*);
    if (::posix_memalign(&p, align, n) != 0) return 0;
    return p;
}

void* zalloc(size_t n) {
    return ::calloc(1, n);
}

void free(void* p, size_t n) {
    (void)n;
    ::free(p);
}

void* realloc(void* p, size_t o, size_t n) {
    (void)o;
    return ::realloc(p, n);
}

char* strdup(const char* s) {
    const size_t n = ::strlen(s) + 1;
    char* const p = (char*) ::malloc(n);
    if (p) ::memcpy(p, s, n);
    return p;
}

} // co
