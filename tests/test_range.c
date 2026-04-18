/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Derrity */
/* Unit tests for ts_range_parse / ts_range_boundary. */
#define _POSIX_C_SOURCE 200809L
#include "range.h"
#include "tinyserve.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void test_simple_range(void) {
    ts_range_t r[8];
    int n = 0;
    assert(ts_range_parse("bytes=0-99", 1000, r, 8, &n) == 0);
    assert(n == 1);
    assert(r[0].start == 0);
    assert(r[0].end == 99);
}

static void test_open_ended(void) {
    ts_range_t r[8];
    int n = 0;
    assert(ts_range_parse("bytes=500-", 1000, r, 8, &n) == 0);
    assert(n == 1);
    assert(r[0].start == 500);
    assert(r[0].end == 999);
}

static void test_suffix(void) {
    ts_range_t r[8];
    int n = 0;
    assert(ts_range_parse("bytes=-200", 1000, r, 8, &n) == 0);
    assert(n == 1);
    assert(r[0].start == 800);
    assert(r[0].end == 999);
}

static void test_suffix_larger_than_file(void) {
    ts_range_t r[8];
    int n = 0;
    assert(ts_range_parse("bytes=-5000", 1000, r, 8, &n) == 0);
    assert(r[0].start == 0);
    assert(r[0].end == 999);
}

static void test_multipart(void) {
    ts_range_t r[8];
    int n = 0;
    assert(ts_range_parse("bytes=0-99,200-299,500-", 1000, r, 8, &n) == 0);
    assert(n == 3);
    assert(r[2].end == 999);
}

static void test_unsatisfiable(void) {
    ts_range_t r[8];
    int n = 0;
    /* Start beyond EOF on the only range -> 416 */
    assert(ts_range_parse("bytes=2000-3000", 1000, r, 8, &n) == -2);
}

static void test_malformed(void) {
    ts_range_t r[8];
    int n = 0;
    assert(ts_range_parse("bytes=abc", 1000, r, 8, &n) == -1);
    assert(ts_range_parse("items=0-10", 1000, r, 8, &n) == -1);
}

static void test_overflow_rejected(void) {
    ts_range_t r[8];
    int n = 0;
    /* 20 nines is well past INT64_MAX (~9.2e18) */
    assert(ts_range_parse("bytes=99999999999999999999-", 1000, r, 8, &n) == -1);
}

static void test_boundary_format_and_uniqueness(void) {
    char a[64], b[64];
    ts_range_boundary(a, sizeof(a));
    ts_range_boundary(b, sizeof(b));
    assert(strncmp(a, "tinyserve_", 10) == 0);
    assert(strncmp(b, "tinyserve_", 10) == 0);
    assert(strlen(a) == 10 + 32);
    for (int i = 10; i < 10 + 32; i++) {
        assert(isxdigit((unsigned char)a[i]));
    }
    /* 128 bits of entropy: collision probability is astronomically low. */
    assert(strcmp(a, b) != 0);
}

int main(void) {
    test_simple_range();
    test_open_ended();
    test_suffix();
    test_suffix_larger_than_file();
    test_multipart();
    test_unsatisfiable();
    test_malformed();
    test_overflow_rejected();
    test_boundary_format_and_uniqueness();
    printf("test_range: PASS\n");
    return 0;
}
