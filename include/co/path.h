#pragma once

#include "fastring.h"

// Ported from golang's path package. The separator is always '/'.

namespace path {

// the shortest path name equivalent to @s (length @n)
//   - path::clean("./x/../..")  ->  ".."
//   - path::clean("x//y//z")    ->  "x/y/z"
fastring clean(const char* s, size_t n);

// @a and @b joined with '/', then cleaned; empty elements are ignored
//   - path::join("/x/", "y")  ->  "/x/y"
fastring join(const char* a, const char* b);

// split after the final slash, so that path == *dir + *file
//   - "/a/b"  ->  "/a/", "b"
void split(const char* s, size_t n, fastring* dir, fastring* file);

// the directory part, cleaned; "." if there is none
fastring dir(const char* s, size_t n);

// the last element, ignoring trailing slashes; "." for "", "/" for "///"
fastring base(const char* s, size_t n);

// the file name extension: "x/x.c" -> ".c", "a/b" -> ""
fastring ext(const char* s, size_t n);

} // path
