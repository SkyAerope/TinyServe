/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Derrity */
#ifndef TINYSERVE_H
#define TINYSERVE_H

#include <uv.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>   /* SOMAXCONN */

#define TS_VERSION "0.3.1"
#define TS_MAX_HEADERS 64
#define TS_MAX_PATH 4096
#define TS_MAX_METHOD 16
#define TS_READ_BUF_SIZE 65536
#define TS_MAX_RANGES 32
#define TS_SEND_BUF_SIZE 65536
#define TS_NOT_FOUND_BODY "Nothing Found in the PATH"
#define TS_MAX_HEADER_SIZE 8192          /* max total header block size */
#define TS_MAX_BODY_SIZE (10*1024*1024)  /* max request body: 10 MB */

/* ── Server-wide tunables ── */
#ifndef TS_DEFAULT_BACKLOG
#  ifdef SOMAXCONN
#    define TS_DEFAULT_BACKLOG SOMAXCONN
#  else
#    define TS_DEFAULT_BACKLOG 1024
#  endif
#endif
#define TS_DEFAULT_MAX_CONNECTIONS 10000 /* per-process cap */
#define TS_DEFAULT_IDLE_TIMEOUT_MS 30000 /* keep-alive idle timeout */
#define TS_DEFAULT_READ_TIMEOUT_MS 10000 /* request read timeout */
#define TS_DEFAULT_WORKERS         0     /* 0 = auto (CPU count) */

/* Forward declarations */
typedef struct ts_client_s ts_client_t;
typedef struct ts_proxy_ctx_s ts_proxy_ctx_t;
typedef struct ts_file_ctx_s ts_file_ctx_t;
typedef struct ts_dirlist_work_s ts_dirlist_work_t;

/* ── Configuration ── */
typedef struct {
    char mode;              /* 'f', 's', 'p' */
    const char *bind_addr;  /* -a, default "127.0.0.1" */
    int port;               /* -p, default 8000 */
    const char *root_dir;   /* -d */
    const char *config_file;/* -c */
    const char *auth_user;  /* -u */
    const char *auth_pass;  /* -w */
    const char *auth_header;/* -k */
    const char *auth_value; /* -v */
    const char *target_host;/* -t */
    int target_port;        /* -q */
    int log_level;          /* 0=error,1=warn,2=info */
    int workers;            /* -j, 0 = auto (CPU count) */
    int max_connections;    /* -n, per-process cap */
    int idle_timeout_ms;    /* keep-alive idle timeout */
    int read_timeout_ms;    /* request read timeout */
} ts_config_t;

/* ── HTTP Header ── */
typedef struct {
    char *name;
    char *value;
} ts_header_t;

/* ── HTTP Request ── */
typedef struct {
    char method[TS_MAX_METHOD];
    char path[TS_MAX_PATH];       /* decoded, normalized */
    char raw_path[TS_MAX_PATH];   /* raw from request line */
    char query[TS_MAX_PATH];      /* query string (after ?) */
    int version_major;
    int version_minor;
    ts_header_t headers[TS_MAX_HEADERS];
    int header_count;
    char *body;
    size_t body_len;
    size_t content_length;
    int keep_alive;
    int headers_complete;
    int complete;
    /* internal parse buffer */
    char *buf;
    size_t buf_len;
    size_t buf_cap;
    size_t parse_pos;  /* current parse offset in buf */
} ts_request_t;

/* ── Range ── */
typedef struct {
    int64_t start;
    int64_t end;  /* inclusive */
} ts_range_t;

/* ── Route (for -m s mode) ── */
typedef struct {
    char method[TS_MAX_METHOD];
    char path[TS_MAX_PATH];
    int status;
    char content_type[256];
    char *body;
    size_t body_len;
    char *extra_headers;  /* additional response headers, \r\n separated */
} ts_route_t;

/* ── Route list ── */
typedef struct {
    ts_route_t *routes;
    int count;
    int capacity;
} ts_route_list_t;

/* ── Client connection ── */
struct ts_client_s {
    uv_tcp_t handle;
    ts_request_t req;
    ts_config_t *config;
    uv_loop_t *loop;
    /* async file serving context (non-NULL while serving a file) */
    ts_file_ctx_t *file_ctx;
    /* async directory listing work item (non-NULL while a uv_queue_work
     * is outstanding for this connection). When ts_client_close fires,
     * the work item's client pointer is cleared so after_work_cb knows
     * to drop its result. */
    ts_dirlist_work_t *dirlist_work;
    /* for route mode */
    ts_route_list_t *routes;
    /* flags */
    int closing;
    /* write completion tracking: response is finished when
     * pending_writes reaches 0 AND response_done is set. */
    int pending_writes;
    int response_done;
    /* per-connection read buffer, used by alloc_cb. Avoids malloc/free
     * churn on every uv_read_start cycle. */
    char read_buf[TS_READ_BUF_SIZE];
    /* keep-alive idle timeout (armed between requests, disarmed on read). */
    uv_timer_t idle_timer;
    int idle_timer_initialized;
    /* request read timeout (armed while a request is in-flight, disarmed
     * once the request line + headers are fully parsed). */
    uv_timer_t read_timer;
    int read_timer_initialized;
    int read_timer_active;
    /* Close coordination: ts_client_close calls uv_close on every
     * initialized handle (tcp + idle_timer [+ future read_timer]).
     * close_pending counts outstanding uv_close callbacks; the client
     * is freed (and g_conn_count decremented) when it hits 0. */
    int close_pending;
};

/* ── Async file serving context ── */
struct ts_file_ctx_s {
    ts_client_t *client;
    uv_fs_t fs_req;
    char *read_buf;           /* TS_SEND_BUF_SIZE, heap-allocated */
    uv_file fd;
    int64_t file_size;
    time_t  mtime;            /* last modified, seconds since epoch */
    char    etag[64];
    char    last_mod[64];     /* RFC 1123 GMT */
    int64_t offset;           /* current read position */
    int64_t remaining;        /* bytes left in current range */
    /* range state */
    ts_range_t ranges[TS_MAX_RANGES];
    int range_count;
    int current_range;        /* index into ranges[] */
    char boundary[64];
    char mime[256];
    int is_multipart;
    int is_head;
};

/* ── Write request with buffer ── */
typedef struct {
    uv_write_t req;
    uv_buf_t buf;
    ts_client_t *client;  /* non-NULL → decrement client->pending_writes on completion */
} ts_write_req_t;

/* ── Proxy context ──
 * State machine for bidirectional TCP forwarding with half-close:
 *   - client_eof / upstream_eof: received EOF from that side
 *   - Shutdown is sent on the opposite side when EOF arrives
 *   - close_count tracks uv_close callbacks; ctx freed when both are done */
struct ts_proxy_ctx_s {
    uv_tcp_t client;
    uv_tcp_t upstream;
    uv_connect_t connect_req;
    uv_shutdown_t client_shutdown_req;
    uv_shutdown_t upstream_shutdown_req;
    ts_config_t *config;

    int upstream_connected;     /* TCP connect succeeded */
    int client_reading;
    int upstream_reading;

    int client_eof;             /* received EOF / error from client */
    int upstream_eof;           /* received EOF / error from upstream */
    int client_write_shut;      /* uv_shutdown sent on client write side */
    int upstream_write_shut;    /* uv_shutdown sent on upstream write side */

    int close_count;            /* incremented in uv_close callback; free ctx at 2 */
    int closing;                /* hard-close in progress */
};

#endif /* TINYSERVE_H */
