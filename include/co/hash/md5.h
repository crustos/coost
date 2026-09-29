/*
 * This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
 * MD5 Message-Digest Algorithm (RFC 1321).
 *
 * Homepage:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * Author:
 * Alexander Peslyak, better known as Solar Designer <solar at openwall.com>
 *
 * This software was written by Alexander Peslyak in 2001.  No copyright is
 * claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed null and void, then the software is
 * Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 * See md5.c for more information.
 */
#pragma once

#include "../fastring.h"

typedef struct {
    uint32 lo, hi;
    uint32 a, b, c, d;
    uint8 buffer[64];
    uint32 block[16];
} md5_ctx_t;

void md5_init(md5_ctx_t* ctx);
void md5_update(md5_ctx_t* ctx, const void* s, size_t n);
void md5_final(md5_ctx_t* ctx, uint8 res[16]);


// md5digest: the 16-byte binary digest, into @res
static inline void md5digest_to(const void* s, size_t n, char res[16]) {
    md5_ctx_t ctx;
    md5_init(&ctx);
    md5_update(&ctx, s, n);
    md5_final(&ctx, (uint8*)res);
}

// md5sum: the digest as 32 lowercase hex characters, into @res
void md5sum_to(const void* s, size_t n, char res[32]);

static inline fastring md5digest(const void* s, size_t n) {
    fastring x(17);
    x.resize(16);
    md5digest_to(s, n, x.data());
    return x;
}

static inline fastring md5sum(const void* s, size_t n) {
    fastring x(33);
    x.resize(32);
    md5sum_to(s, n, x.data());
    return x;
}
