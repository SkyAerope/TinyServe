/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Derrity */
/* Unit tests for the streaming HTTP request parser. */
#define _POSIX_C_SOURCE 200809L
#include "http_parser.h"
#include "tinyserve.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse_full(ts_request_t *req, const char *s) {
    int rc = ts_request_parse(req, s, strlen(s));
    assert(rc > 0);
}

static void test_simple_get(void) {
    ts_request_t req; ts_request_init(&req);
    parse_full(&req, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    assert(strcmp(req.method, "GET") == 0);
    assert(strcmp(req.path, "/") == 0);
    assert(req.version_major == 1 && req.version_minor == 1);
    const char *h = ts_request_header(&req, "host");
    assert(h && strcmp(h, "x") == 0);
    ts_request_free(&req);
}

static void test_header_case_insensitive(void) {
    ts_request_t req; ts_request_init(&req);
    parse_full(&req, "GET / HTTP/1.1\r\nContent-Type: text/plain\r\n\r\n");
    const char *v = ts_request_header(&req, "CONTENT-TYPE");
    assert(v && strcmp(v, "text/plain") == 0);
    ts_request_free(&req);
}

static void test_two_chunk_delivery(void) {
    /* Parser may not support arbitrary mid-line resumption, but it must
     * accept data that completes a header section across two write calls. */
    ts_request_t req; ts_request_init(&req);
    const char *part1 = "GET /foo HTTP/1.1\r\nHost: a\r\n";
    const char *part2 = "\r\n";
    int rc = ts_request_parse(&req, part1, strlen(part1));
    assert(rc == 0);
    rc = ts_request_parse(&req, part2, strlen(part2));
    assert(rc > 0);
    assert(strcmp(req.path, "/foo") == 0);
    ts_request_free(&req);
}

static void test_oversize_headers_returns_minus_two(void) {
    ts_request_t req; ts_request_init(&req);
    /* Build a header section larger than TS_MAX_HEADER_SIZE. */
    size_t blob = (size_t)TS_MAX_HEADER_SIZE + 1024;
    char *big = malloc(blob);
    assert(big);
    memset(big, 'a', blob);
    int rc = ts_request_parse(&req, "GET / HTTP/1.1\r\nX: ", 19);
    assert(rc == 0);
    rc = ts_request_parse(&req, big, blob);
    assert(rc == -2);
    free(big);
    ts_request_free(&req);
}

static void test_invalid_request_line(void) {
    ts_request_t req; ts_request_init(&req);
    int rc = ts_request_parse(&req, "GARBAGE\r\n\r\n", 11);
    assert(rc == -1);
    ts_request_free(&req);
}

static void test_reset_for_keepalive(void) {
    ts_request_t req; ts_request_init(&req);
    parse_full(&req, "GET /a HTTP/1.1\r\nHost: x\r\n\r\n");
    assert(strcmp(req.path, "/a") == 0);
    ts_request_reset(&req);
    parse_full(&req, "GET /b HTTP/1.1\r\nHost: y\r\n\r\n");
    assert(strcmp(req.path, "/b") == 0);
    const char *h = ts_request_header(&req, "host");
    assert(h && strcmp(h, "y") == 0);
    ts_request_free(&req);
}

static void test_content_length_body(void) {
    ts_request_t req; ts_request_init(&req);
    parse_full(&req, "POST /x HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\n\r\nhello");
    assert(req.body_len == 5);
    assert(req.body && memcmp(req.body, "hello", 5) == 0);
    ts_request_free(&req);
}

static void test_smuggling_cl_te_rejected(void) {
    ts_request_t req; ts_request_init(&req);
    const char *m =
        "POST / HTTP/1.1\r\nHost: a\r\n"
        "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n";
    int rc = ts_request_parse(&req, m, strlen(m));
    assert(rc == -1);
    ts_request_free(&req);
}

static void test_invalid_header_name_rejected(void) {
    ts_request_t req; ts_request_init(&req);
    /* Space in header name is forbidden by RFC 7230 \u00a73.2.6. */
    const char *m = "GET / HTTP/1.1\r\nBad Name: x\r\n\r\n";
    int rc = ts_request_parse(&req, m, strlen(m));
    assert(rc == -1);
    ts_request_free(&req);
}

static void test_nul_in_path_rejected(void) {
    ts_request_t req; ts_request_init(&req);
    /* Embedded NUL byte in the request-target. */
    const char m[] = "GET /a\0b HTTP/1.1\r\nHost: x\r\n\r\n";
    int rc = ts_request_parse(&req, m, sizeof(m) - 1);
    assert(rc == -1);
    ts_request_free(&req);
}

int main(void) {
    test_nul_in_path_rejected();
    test_simple_get();
    test_header_case_insensitive();
    test_two_chunk_delivery();
    test_oversize_headers_returns_minus_two();
    test_invalid_request_line();
    test_reset_for_keepalive();
    test_content_length_body();
    test_smuggling_cl_te_rejected();
    test_invalid_header_name_rejected();
    printf("test_http_parser: PASS\n");
    return 0;
}
