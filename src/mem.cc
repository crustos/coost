#include "co/mem.h"

namespace co {

static void* checked(void* p) {
    if (p == 0) ::abort();
    return p;
}

void* alloc(size_t n) {
    return checked(::malloc(n ? n : 1));
}

void* zalloc(size_t n) {
    return checked(::calloc(1, n ? n : 1));
}

void free(void* p, size_t n) {
    (void)n;
    ::free(p);
}

void* realloc(void* p, size_t o, size_t n) {
    (void)o;
    return checked(::realloc(p, n ? n : 1));
}

char* strdup(const char* s) {
    const size_t n = ::strlen(s) + 1;
    char* const p = (char*) alloc(n);
    ::memcpy(p, s, n);
    return p;
}

} // co
