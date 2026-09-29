#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(x) do { g_checks++; if (!(x)) { g_fails++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

#define CHECK_STR(s, lit) CHECK(strcmp((s), (lit)) == 0)

static int test_report(const char* name) {
    printf("  %s: %d checks, %d failed\n", name, g_checks, g_fails);
    return g_fails ? 1 : 0;
}
