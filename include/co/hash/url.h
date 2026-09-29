#pragma once

#include "../fastring.h"

// Characters left as-is: a-z A-Z 0-9 - _ . ~ and the reserved set
// ! ( ) * # $ & ' + , / : ; = ? @ [ ]
fastring url_encode(const void* s, size_t n);

// returns an empty string if the input is empty or not validly encoded
fastring url_decode(const void* s, size_t n);
