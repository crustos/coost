#pragma once

#include "def.h"
#include "hash/murmur_hash.h"
#include "hash/crc16.h"
#include "hash/md5.h"
#include "hash/sha256.h"
#include "hash/base64.h"
#include "hash/url.h"

// 64 bit hash of the @n bytes at @s
static inline uint64 hash64(const void* s, size_t n) {
    return murmur_hash64(s, n, 0);
}

// 32 bit hash: the low 32 bits of murmur_hash
static inline uint32 hash32(const void* s, size_t n) {
    return (uint32) murmur_hash(s, n, 0);
}
