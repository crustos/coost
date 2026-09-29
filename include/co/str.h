#pragma once

#include "fastring.h"
#include "vector.h"

namespace str {

// @s (length @n) with @sub replaced by @to, at most @t times (0 for all)
//   - str::replace("xooxoox", 7, "oo", "ee", 0);  ->  "xeexeex"
//   - str::replace("xooxoox", 7, "oo", "ee", 1);  ->  "xeexoox"
fastring replace(const char* s, size_t n, const char* sub, const char* to, size_t t);

// split @s (length @n) on the character @c, at most @t times (0 for all)
//   - str::split("|x|y|", 5, '|', 0);  ->  [ "", "x", "y" ]
//   - str::split("xooy", 4, 'o', 1);   ->  [ "x", "oy" ]
co::vector<fastring> split(const char* s, size_t n, char c, size_t t);

// split @s (length @n) on the string @sep, at most @t times (0 for all)
co::vector<fastring> split_cstr(const char* s, size_t n, const char* sep, size_t t);

// String to number. On failure these return false or 0 and set errno to
// EINVAL or ERANGE; on success errno is 0. Integers accept a k/m/g/t/p
// suffix (powers of 1024) and a 0x or 0 prefix.
bool to_bool(const char* s);
int32 to_int32(const char* s);
int64 to_int64(const char* s);
uint32 to_uint32(const char* s);
uint64 to_uint64(const char* s);
double to_double(const char* s);

} // str
