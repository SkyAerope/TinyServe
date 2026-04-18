/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Derrity */
/* Smoke test: verifies the CTest harness itself works. */
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(1 + 1 == 2);
    printf("smoke: OK\n");
    return 0;
}
