/**
 * Sha256.h -- SHA-256 Hash
 * 2010-06-11 : Igor Pavlov : Public domain
 * 2022-05-23 : Modified by Alvin.
 */
#pragma once

#include "../fastring.h"

typedef struct {
    uint32 state[8];
    uint64 count;
    uint8 buffer[64];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t* ctx);
void sha256_update(sha256_ctx_t* ctx, const void* s, size_t n);
void sha256_final(sha256_ctx_t* ctx, uint8 res[32]);


// sha256digest: the 32-byte binary digest, into @res
static inline void sha256digest_to(const void* s, size_t n, char res[32]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, s, n);
    sha256_final(&ctx, (uint8*)res);
}

// sha256sum: the digest as 64 lowercase hex characters, into @res
void sha256sum_to(const void* s, size_t n, char res[64]);

static inline fastring sha256digest(const void* s, size_t n) {
    fastring x(33);
    x.resize(32);
    sha256digest_to(s, n, x.data());
    return x;
}

static inline fastring sha256sum(const void* s, size_t n) {
    fastring x(65);
    x.resize(64);
    sha256sum_to(s, n, x.data());
    return x;
}
