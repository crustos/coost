#pragma once

// Allocation helpers. Thin wrappers over the C allocator; the sized
// signatures are kept so callers stay compatible with upstream coost.
// Out of memory aborts: nothing in coost can recover from it, and callers
// then need no null checks (and no <assert.h>, which crust's C compiler
// does not provide).

#include "def.h"
#include <stdlib.h>
#include <string.h>

namespace co {

// alloc @size bytes
void* alloc(size_t size);

// alloc @size bytes, and zero-clear the memory
void* zalloc(size_t size);

// free memory from co::alloc/co::realloc; @size is the allocation size
void free(void* p, size_t size);

// grow memory from co::alloc(); if p is NULL, same as co::alloc(new_size)
void* realloc(void* p, size_t old_size, size_t new_size);

char* strdup(const char* s);

} // co
