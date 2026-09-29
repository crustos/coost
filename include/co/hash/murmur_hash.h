/*
 * Murmurhash from http://sites.google.com/site/murmurhash.
 * Written by Austin Appleby, and is placed to the public domain.
 * For business purposes, Murmurhash is under the MIT license.
 */
#pragma once

#include "../def.h"

uint32_t murmur_hash32(const void* s, size_t n, uint32_t seed);
uint64_t murmur_hash64(const void* s, size_t n, uint64_t seed);

// size_t-wide murmur hash: 64 bit on 64 bit platforms, 32 bit otherwise
static inline size_t murmur_hash(const void* s, size_t n, size_t seed) {
#if __arch64
    return (size_t) murmur_hash64(s, n, (uint64_t)seed);
#else
    return (size_t) murmur_hash32(s, n, (uint32_t)seed);
#endif
}
