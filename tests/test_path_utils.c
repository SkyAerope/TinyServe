/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Derrity */
/* Unit tests for ts_path_resolve / ts_url_decode.
 * Validates URL decoding rejection of NUL bytes, traversal protection,
 * normalization of redundant separators and dot-segments. */
#define _POSIX_C_SOURCE 200809L
#include "path_utils.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_root[256];

static void make_root(void) {
    snprintf(g_root, sizeof(g_root), "/tmp/ts-test-path-%d", (int)getpid());
    mkdir(g_root, 0700);
    char sub[512];
    snprintf(sub, sizeof(sub), "%s/sub", g_root);
    mkdir(sub, 0700);
    snprintf(sub, sizeof(sub), "%s/sub/file.txt", g_root);
    FILE *f = fopen(sub, "w");
    if (f) { fputs("hi", f); fclose(f); }
}

static void cleanup_root(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_root);
    int r = system(cmd);
    (void)r;
}

static void test_url_decode_basic(void) {
    char out[64];
    assert(ts_url_decode("/foo%20bar", out, sizeof(out)) == 0);
    assert(strcmp(out, "/foo bar") == 0);
    assert(ts_url_decode("/a%2Fb", out, sizeof(out)) == 0);
    assert(strcmp(out, "/a/b") == 0);
    /* '+' must NOT decode to space in URL paths */
    assert(ts_url_decode("a+b", out, sizeof(out)) == 0);
    assert(strcmp(out, "a+b") == 0);
}

static void test_url_decode_rejects_nul(void) {
    char out[64];
    /* %00 must be rejected to prevent NUL truncation attacks */
    assert(ts_url_decode("/a%00b", out, sizeof(out)) == -1);
}

static void test_url_decode_invalid_hex(void) {
    char out[64];
    assert(ts_url_decode("%zz", out, sizeof(out)) == -1);
    assert(ts_url_decode("%2", out, sizeof(out)) == -1);
    assert(ts_url_decode("%", out, sizeof(out)) == -1);
}

static void test_path_resolve_normal(void) {
    char out[PATH_MAX];
    assert(ts_path_resolve(g_root, "/sub/file.txt", out, sizeof(out)) == 0);
    char want[PATH_MAX];
    char real[PATH_MAX];
    assert(realpath(g_root, real) != NULL);
    snprintf(want, sizeof(want), "%s/sub/file.txt", real);
    assert(strcmp(out, want) == 0);
}

static void test_path_resolve_traversal_dotdot(void) {
    char out[PATH_MAX];
    /* Direct traversal */
    assert(ts_path_resolve(g_root, "/../etc/passwd", out, sizeof(out)) == -1);
    /* Encoded traversal */
    assert(ts_path_resolve(g_root, "/sub/../../etc/passwd", out, sizeof(out)) == -1);
    /* URL-encoded slashes around .. */
    assert(ts_path_resolve(g_root, "/%2e%2e/%2e%2e/etc/passwd", out, sizeof(out)) == -1);
}

static void test_path_resolve_backslash_normalized(void) {
    char out[PATH_MAX];
    /* Backslash treated as separator and normalized; the resulting path
     * inside root must still be valid. */
    assert(ts_path_resolve(g_root, "/sub\\file.txt", out, sizeof(out)) == 0);
}

static void test_path_resolve_collapses_slashes(void) {
    char out[PATH_MAX];
    assert(ts_path_resolve(g_root, "//sub///file.txt", out, sizeof(out)) == 0);
}

static void test_path_resolve_strips_query(void) {
    char out[PATH_MAX];
    assert(ts_path_resolve(g_root, "/sub/file.txt?x=1&y=2", out, sizeof(out)) == 0);
}

int main(void) {
    make_root();
    test_url_decode_basic();
    test_url_decode_rejects_nul();
    test_url_decode_invalid_hex();
    test_path_resolve_normal();
    test_path_resolve_traversal_dotdot();
    test_path_resolve_backslash_normalized();
    test_path_resolve_collapses_slashes();
    test_path_resolve_strips_query();
    cleanup_root();
    printf("test_path_utils: PASS\n");
    return 0;
}
