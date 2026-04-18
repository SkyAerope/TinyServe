/* Unit tests for src/config.c */
#include "config.h"
#include "tinyserve.h"
#include "log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_defaults(void) {
    ts_config_t cfg;
    ts_config_defaults(&cfg);
    assert(cfg.mode == 'f');
    assert(strcmp(cfg.bind_addr, "127.0.0.1") == 0);
    assert(cfg.port == 8000);
    assert(cfg.workers == TS_DEFAULT_WORKERS);
    assert(cfg.max_connections == TS_DEFAULT_MAX_CONNECTIONS);
    assert(cfg.idle_timeout_ms == TS_DEFAULT_IDLE_TIMEOUT_MS);
    assert(cfg.read_timeout_ms == TS_DEFAULT_READ_TIMEOUT_MS);
}

static void test_parse_workers_and_maxconn(void) {
    ts_config_t cfg;
    ts_config_defaults(&cfg);
    char *argv[] = {(char*)"t", (char*)"-j", (char*)"4",
                    (char*)"-n", (char*)"500", NULL};
    int argc = 5;
    int r = ts_config_parse(argc, argv, &cfg);
    assert(r == 0);
    assert(cfg.workers == 4);
    assert(cfg.max_connections == 500);
}

static void test_parse_rejects_bad_workers(void) {
    ts_config_t cfg;
    ts_config_defaults(&cfg);
    char *argv[] = {(char*)"t", (char*)"-j", (char*)"abc", NULL};
    int r = ts_config_parse(3, argv, &cfg);
    assert(r == -1);
}

static void test_parse_rejects_negative_maxconn(void) {
    ts_config_t cfg;
    ts_config_defaults(&cfg);
    char *argv[] = {(char*)"t", (char*)"-n", (char*)"-1", NULL};
    int r = ts_config_parse(3, argv, &cfg);
    assert(r == -1);
}

static void test_parse_rejects_zero_maxconn(void) {
    ts_config_t cfg;
    ts_config_defaults(&cfg);
    char *argv[] = {(char*)"t", (char*)"-n", (char*)"0", NULL};
    int r = ts_config_parse(3, argv, &cfg);
    assert(r == -1);
}

int main(void) {
    ts_log_init(TS_LOG_ERROR);
    test_defaults();
    test_parse_workers_and_maxconn();
    test_parse_rejects_bad_workers();
    test_parse_rejects_negative_maxconn();
    test_parse_rejects_zero_maxconn();
    printf("config: OK\n");
    return 0;
}
