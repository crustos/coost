#pragma once

#include "../fastring.h"

fastring base64_encode(const void* s, size_t n);

// returns an empty string if the input is empty or not valid base64
fastring base64_decode(const void* s, size_t n);
