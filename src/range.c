#include "range.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#if defined(TS_HAVE_GETRANDOM)
#  include <sys/random.h>
#endif

/* Fill buf with len cryptographically strong random bytes.
 * Tries getrandom(2), then arc4random_buf(3), then /dev/urandom. */
static int ts_secure_random(void *buf, size_t len) {
#if defined(TS_HAVE_GETRANDOM)
    {
        size_t got = 0;
        while (got < len) {
            ssize_t r = getrandom((char *)buf + got, len - got, 0);
            if (r < 0) { if (errno == EINTR) continue; break; }
            got += (size_t)r;
        }
        if (got == len) return 0;
    }
#endif
#if defined(TS_HAVE_ARC4RANDOM_BUF)
    arc4random_buf(buf, len);
    return 0;
#else
    {
        int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd < 0) return -1;
        size_t got = 0;
        while (got < len) {
            ssize_t r = read(fd, (char *)buf + got, len - got);
            if (r < 0) { if (errno == EINTR) continue; close(fd); return -1; }
            if (r == 0) { close(fd); return -1; }
            got += (size_t)r;
        }
        close(fd);
        return 0;
    }
#endif
}

/* Safely accumulate a digit into an int64_t.
 * Returns 0 on success, -1 on overflow. */
static int safe_accum(int64_t *val, int digit)
{
    /* INT64_MAX = 9223372036854775807 */
    if (*val > (INT64_MAX - digit) / 10)
        return -1;
    *val = *val * 10 + digit;
    return 0;
}

int ts_range_parse(const char *header, int64_t file_size,
                   ts_range_t *ranges, int max_ranges, int *count) {
    *count = 0;

    /* Must start with "bytes=" (case-insensitive) */
    if (strncasecmp(header, "bytes=", 6) != 0)
        return -1;

    const char *p = header + 6;

    while (*p) {
        /* trim leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        int64_t start = -1, end = -1;

        if (*p == '-') {
            /* suffix range: -N */
            p++;
            if (*p < '0' || *p > '9') return -1;
            int64_t suffix = 0;
            while (*p >= '0' && *p <= '9') {
                if (safe_accum(&suffix, *p - '0') != 0)
                    return -1;  /* overflow */
                p++;
            }
            if (suffix == 0) return -1;
            if (suffix > file_size) suffix = file_size;
            start = file_size - suffix;
            end = file_size - 1;
        } else if (*p >= '0' && *p <= '9') {
            /* start-end or start- */
            start = 0;
            while (*p >= '0' && *p <= '9') {
                if (safe_accum(&start, *p - '0') != 0)
                    return -1;  /* overflow */
                p++;
            }
            if (*p != '-') return -1;
            p++;
            /* trim whitespace after dash */
            while (*p == ' ' || *p == '\t') p++;

            if (*p >= '0' && *p <= '9') {
                end = 0;
                while (*p >= '0' && *p <= '9') {
                    if (safe_accum(&end, *p - '0') != 0)
                        return -1;  /* overflow */
                    p++;
                }
                if (end >= file_size)
                    end = file_size - 1;
            } else {
                /* open-ended: start to EOF */
                end = file_size - 1;
            }

            if (start >= file_size) {
                /* skip to next range spec */
                while (*p == ' ' || *p == '\t') p++;
                if (*p == ',') { p++; continue; }
                break;
            }
            if (start > end) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p == ',') { p++; continue; }
                break;
            }
        } else {
            return -1;
        }

        /* Validate */
        if (start < 0 || start >= file_size) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == ',') { p++; continue; }
            break;
        }

        ranges[*count].start = start;
        ranges[*count].end = end;
        (*count)++;

        if (*count >= max_ranges) break;

        /* skip to next comma */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ',') {
            p++;
        } else {
            break;
        }
    }

    if (*count == 0)
        return -2;

    return 0;
}

void ts_range_boundary(char *buf, size_t buf_size) {
    const char *prefix = "tinyserve_";
    size_t plen = strlen(prefix);
    size_t hex_len = 32;  /* 128 bits of entropy */

    if (buf_size < plen + hex_len + 1) {
        buf[0] = '\0';
        return;
    }

    unsigned char rnd[16];
    if (ts_secure_random(rnd, sizeof(rnd)) != 0) {
        LOG_ERROR("secure random source unavailable for boundary");
        buf[0] = '\0';
        return;
    }

    memcpy(buf, prefix, plen);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(rnd); i++) {
        buf[plen + i * 2]     = hex[(rnd[i] >> 4) & 0xF];
        buf[plen + i * 2 + 1] = hex[rnd[i]        & 0xF];
    }
    buf[plen + hex_len] = '\0';
}

int64_t ts_range_multipart_size(const ts_range_t *ranges, int count,
                                const char *content_type,
                                int64_t file_size,
                                const char *boundary) {
    int64_t total = 0;
    char line[512];

    for (int i = 0; i < count; i++) {
        /* \r\n--BOUNDARY\r\n */
        total += 2 + 2 + (int64_t)strlen(boundary) + 2;

        /* Content-Type: TYPE\r\n */
        int n = snprintf(line, sizeof(line), "Content-Type: %s\r\n", content_type);
        total += n;

        /* Content-Range: bytes START-END/TOTAL\r\n */
        n = snprintf(line, sizeof(line), "Content-Range: bytes %lld-%lld/%lld\r\n",
                     (long long)ranges[i].start, (long long)ranges[i].end,
                     (long long)file_size);
        total += n;

        /* \r\n (blank line before body) */
        total += 2;

        /* body data */
        total += ranges[i].end - ranges[i].start + 1;
    }

    /* \r\n--BOUNDARY--\r\n */
    total += 2 + 2 + (int64_t)strlen(boundary) + 2 + 2;

    return total;
}
