#pragma once

// Allocation helpers. Thin wrappers over the C allocator; the sized
// signatures are kept so callers stay compatible with upstream coost.

#include "def.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

namespace co {

// alloc @size bytes
void* alloc(size_t size);

// alloc @size bytes, @align byte aligned (align must be a power of 2)
void* alloc_aligned(size_t size, size_t align);

// alloc @size bytes, and zero-clear the memory
void* zalloc(size_t size);

// free memory from co::alloc/co::realloc; @size is the allocation size
void free(void* p, size_t size);

// grow memory from co::alloc(); if p is NULL, same as co::alloc(new_size)
void* realloc(void* p, size_t old_size, size_t new_size);

char* strdup(const char* s);

} // co
