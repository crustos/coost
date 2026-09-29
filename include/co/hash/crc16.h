#pragma once

#include "../def.h"

// crc16 of the @n bytes at @s, continuing from @crc (0 to start)
uint16_t crc16(const void* s, size_t n, uint16_t crc);
