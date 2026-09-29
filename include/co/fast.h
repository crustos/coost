#pragma once

// Fast number to string conversion. Each function writes into @buf and
// returns the number of characters written; no terminating '\0' is added.

#include "def.h"
#include "__/dtoa_milo.h"

namespace fast {

// double to ascii string, at most @mdp decimal places (324 for all)
static inline int dtoa(double v, char* buf, int mdp) {
    return milo::dtoa(v, buf, mdp);
}

// unsigned integer to hex string (e.g. 255 -> 0xff)
int u32toh(uint32 v, char* buf);
int u64toh(uint64 v, char* buf);

// integer to decimal string
int u32toa(uint32 v, char* buf);
int u64toa(uint64 v, char* buf);

static inline int i32toa(int32 v, char* buf) {
    if (v >= 0) return u32toa((uint32)v, buf);
    *buf = '-';
    return u32toa((uint32)0 - (uint32)v, buf + 1) + 1;
}

static inline int i64toa(int64 v, char* buf) {
    if (v >= 0) return u64toa((uint64)v, buf);
    *buf = '-';
    return u64toa((uint64)0 - (uint64)v, buf + 1) + 1;
}

// pointer to hex string
static inline int ptoh(const void* p, char* buf) {
    return u64toh((uint64)(size_t)p, buf);
}

} // fast
