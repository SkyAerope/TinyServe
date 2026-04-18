/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Derrity */
#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static int g_log_level = TS_LOG_INFO;

void ts_log_init(int level) {
    if (level < TS_LOG_ERROR) level = TS_LOG_ERROR;
    if (level > TS_LOG_INFO) level = TS_LOG_INFO;
    g_log_level = level;
}

void ts_log(int level, const char *fmt, ...) {
    if (level > g_log_level) return;

    static const char *prefixes[] = { "ERROR", "WARN", "INFO" };
    const char *prefix = (level >= TS_LOG_ERROR && level <= TS_LOG_INFO)
                         ? prefixes[level] : "???";

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    /* Format the user message into a private buffer first so we can
     * sanitize control bytes \u2014 prevents log-injection where an
     * attacker-supplied path contains "\r\n[INFO] FAKE LINE". */
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) { msg[0] = '\0'; n = 0; }
    if ((size_t)n >= sizeof(msg)) n = (int)sizeof(msg) - 1;

    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)msg[i];
        /* Allow tab; replace every other C0 control byte (incl. CR/LF)
         * and DEL with '.'. Leaves UTF-8 bytes (>= 0x80) untouched. */
        if (c == '\t') continue;
        if (c < 0x20 || c == 0x7f) msg[i] = '.';
    }

    fprintf(stderr, "[%02d:%02d:%02d] [%s] %s\n",
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, prefix, msg);
}
