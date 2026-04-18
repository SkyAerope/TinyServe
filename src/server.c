/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Derrity */
#include "server.h"
#include "log.h"
#include "http_parser.h"
#include "http_response.h"
#include "file_serve.h"
#include "route.h"
#include "proxy.h"
#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

static uv_loop_t *loop;
static uv_tcp_t server_handle;
static ts_config_t *g_config;
static ts_route_list_t g_routes;

static uv_signal_t sigint_handle, sigterm_handle;

/* Active connection counter (process-wide). Incremented when accept
 * succeeds; decremented in the client close callback. */
static atomic_int g_conn_count = 0;

static ts_client_t *client_alloc(void) {
    ts_client_t *client = calloc(1, sizeof(ts_client_t));
    if (!client) return NULL;
    client->config = g_config;
    client->loop = loop;
    client->file_ctx = NULL;
    client->routes = &g_routes;
    client->pending_writes = 0;
    client->response_done = 0;
    ts_request_init(&client->req);
    return client;
}

static void client_final_free(ts_client_t *client) {
    ts_request_free(&client->req);
    free(client);
    atomic_fetch_sub(&g_conn_count, 1);
}

static void on_handle_closed(uv_handle_t *handle) {
    ts_client_t *client = handle->data;
    if (!client) return;
    if (--client->close_pending == 0) {
        client_final_free(client);
    }
}

static void alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void dispatch_request(ts_client_t *client);

static void on_idle_timeout(uv_timer_t *timer) {
    ts_client_t *client = timer->data;
    if (!client || client->closing) return;
    ts_client_close(client);
}

static void on_read_timeout(uv_timer_t *timer) {
    ts_client_t *client = timer->data;
    if (!client || client->closing) return;
    /* Partial request: send 408 if possible, then close. */
    client->req.keep_alive = 0;
    ts_response_send(client, 408, "text/plain",
                     "Request Timeout", 15, NULL, 0);
}

static void idle_timer_start(ts_client_t *client) {
    if (!client->idle_timer_initialized || client->closing) return;
    int ms = client->config ? client->config->idle_timeout_ms
                            : TS_DEFAULT_IDLE_TIMEOUT_MS;
    if (ms <= 0) return;
    uv_timer_start(&client->idle_timer, on_idle_timeout, (uint64_t)ms, 0);
}

static void idle_timer_stop(ts_client_t *client) {
    if (!client->idle_timer_initialized) return;
    uv_timer_stop(&client->idle_timer);
}

static void read_timer_start(ts_client_t *client) {
    if (!client->read_timer_initialized || client->closing) return;
    if (client->read_timer_active) return;
    int ms = client->config ? client->config->read_timeout_ms
                            : TS_DEFAULT_READ_TIMEOUT_MS;
    if (ms <= 0) return;
    uv_timer_start(&client->read_timer, on_read_timeout, (uint64_t)ms, 0);
    client->read_timer_active = 1;
}

static void read_timer_stop(ts_client_t *client) {
    if (!client->read_timer_initialized) return;
    uv_timer_stop(&client->read_timer);
    client->read_timer_active = 0;
}

void ts_client_close(ts_client_t *client) {
    if (client->closing) return;
    client->closing = 1;

    /* Detach any outstanding directory listing work so its completion
     * callback knows to drop the result. */
    ts_file_serve_detach(client);

    /* Clean up any in-progress file context */
    if (client->file_ctx) {
        if (client->file_ctx->fd >= 0) {
            uv_fs_t close_req;
            uv_fs_close(client->loop, &close_req, client->file_ctx->fd, NULL);
            uv_fs_req_cleanup(&close_req);
        }
        free(client->file_ctx->read_buf);
        free(client->file_ctx);
        client->file_ctx = NULL;
    }

    client->close_pending = 0;
    if (!uv_is_closing((uv_handle_t *)&client->handle)) {
        client->close_pending++;
        uv_close((uv_handle_t *)&client->handle, on_handle_closed);
    }
    if (client->idle_timer_initialized &&
        !uv_is_closing((uv_handle_t *)&client->idle_timer)) {
        uv_timer_stop(&client->idle_timer);
        client->close_pending++;
        uv_close((uv_handle_t *)&client->idle_timer, on_handle_closed);
    }
    if (client->read_timer_initialized &&
        !uv_is_closing((uv_handle_t *)&client->read_timer)) {
        uv_timer_stop(&client->read_timer);
        client->read_timer_active = 0;
        client->close_pending++;
        uv_close((uv_handle_t *)&client->read_timer, on_handle_closed);
    }
    if (client->close_pending == 0) {
        /* Nothing to close (should not happen in practice). */
        client_final_free(client);
    }
}

/* ── Called when pending_writes hits 0 and response_done is set ── */
void ts_client_done(ts_client_t *client) {
    if (client->closing) return;

    /* Reset write tracking for next request */
    client->pending_writes = 0;
    client->response_done = 0;

    if (client->req.keep_alive) {
        ts_request_reset(&client->req);

        /* If there is leftover data from pipelining, try to parse it */
        if (client->req.buf_len > 0) {
            int result = ts_request_parse(&client->req, NULL, 0);
            if (result < 0) {
                /* Parse error on leftover data */
                ts_response_send(client, 400, "text/plain",
                                 "Bad Request", 11, NULL, 0);
                /* response_done is set by ts_response_send; close after write */
                client->req.keep_alive = 0;
                return;
            }
            if (result > 0) {
                /* Complete pipelined request ready — dispatch it */
                LOG_INFO("%s %s", client->req.method, client->req.raw_path);
                dispatch_request(client);
                return;
            }
            /* result == 0: need more data, fall through to uv_read_start */
        }

        uv_read_start((uv_stream_t *)&client->handle, alloc_cb, on_read);
        idle_timer_start(client);
    } else {
        ts_client_close(client);
    }
}

/* ── Mark response as fully queued ── */
void ts_client_response_end(ts_client_t *client) {
    if (client->closing) return;
    client->response_done = 1;
    if (client->pending_writes == 0) {
        ts_client_done(client);
    }
    /* else: ts_client_done will be called from write completion callback */
}

static void alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    (void)suggested_size;
    ts_client_t *client = handle->data;
    buf->base = client->read_buf;
    buf->len  = sizeof(client->read_buf);
}

/* Dispatch a fully-parsed request to the appropriate handler */
static void dispatch_request(ts_client_t *client) {
    /* Stop reading while request is being served */
    uv_read_stop((uv_stream_t *)&client->handle);

    if (client->config->mode == 'f') {
        ts_file_serve(client);
    } else if (client->config->mode == 's') {
        ts_route_serve(client);
    }
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    ts_client_t *client = stream->data;

    if (nread < 0) {
        ts_client_close(client);
        return;
    }

    if (nread == 0) {
        return;
    }
/* Activity on the connection: cancel the keep-alive idle deadline
     * and start the request read deadline for this request's bytes. */
    idle_timer_stop(client);
    read_timer_start(client);

    int result = ts_request_parse(&client->req, buf->base, nread);

    if (result == -2) {
        /* Header section exceeded TS_MAX_HEADER_SIZE — RFC 6585 §5. */
        client->req.keep_alive = 0;
        ts_response_send(client, 431, "text/plain",
                         "Request Header Fields Too Large", 31, NULL, 0);
        return;
    }

    if (result < 0) {
        /* Parse error — send 400 and close.
         * ts_response_send sets response_done; keep_alive=0 ensures close. */
        client->req.keep_alive = 0;
        ts_response_send(client, 400, "text/plain",
                         "Bad Request", 11, NULL, 0);
        return;
    }

    if (result == 0) {
        return;  /* need more data */
    }

    LOG_INFO("%s %s", client->req.method, client->req.raw_path);
    read_timer_stop(client);
    dispatch_request(client);
}

static void on_connection(uv_stream_t *server, int status) {
    if (status < 0) {
        LOG_ERROR("connection error: %s", uv_strerror(status));
        return;
    }

    if (g_config->mode == 'p') {
        ts_proxy_handle_connection(server, g_config, loop);
        return;
    }
/* Enforce max connections cap. We accept first so the kernel's
     * accept queue doesn't fill up with stale SYNs, then either serve
     * a 503 or reject. */
    int prev = atomic_fetch_add(&g_conn_count, 1);
    if (prev >= g_config->max_connections) {
        atomic_fetch_sub(&g_conn_count, 1);
        /* Accept and immediately close to drain the accept queue. */
        uv_tcp_t *drop = calloc(1, sizeof(*drop));
        if (!drop) return;
        uv_tcp_init(loop, drop);
        if (uv_accept(server, (uv_stream_t *)drop) == 0) {
            static const char body[] =
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 19\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Service Unavailable";
            uv_buf_t b = uv_buf_init((char *)body, sizeof(body) - 1);
            uv_try_write((uv_stream_t *)drop, &b, 1);
        }
        uv_close((uv_handle_t *)drop, (uv_close_cb)free);
        LOG_WARN("connection cap reached (%d), sent 503",
                 g_config->max_connections);
        return;
    }

    ts_client_t *client = client_alloc();
    if (!client) {
        atomic_fetch_sub(&g_conn_count, 1);
        LOG_ERROR("failed to allocate client");
        return;
    }

    uv_tcp_init(loop, &client->handle);
    client->handle.data = client;

    if (uv_accept(server, (uv_stream_t *)&client->handle) != 0) {
        LOG_ERROR("failed to accept connection");
        ts_client_close(client);
        return;
    }

    uv_timer_init(loop, &client->idle_timer);
    client->idle_timer.data = client;
    client->idle_timer_initialized = 1;

    uv_timer_init(loop, &client->read_timer);
    client->read_timer.data = client;
    client->read_timer_initialized = 1;

    uv_read_start((uv_stream_t *)&client->handle, alloc_cb, on_read);
    idle_timer_start(client);
}

static void on_signal(uv_signal_t *handle, int signum) {
    (void)handle;
    LOG_INFO("received signal %d, shutting down...", signum);
    uv_signal_stop(&sigint_handle);
    uv_signal_stop(&sigterm_handle);
    uv_close((uv_handle_t *)&server_handle, NULL);
}

int ts_server_start(ts_config_t *cfg);

/* Determine the effective worker count.
 *   - cfg->workers >  0  : honoured as-is (capped to 1 on macOS, see below).
 *   - cfg->workers == 0  : auto = sysconf(_SC_NPROCESSORS_ONLN) on Linux,
 *                          1 on every other platform.
 * macOS' SO_REUSEPORT does NOT load-balance accepts across listeners,
 * so multi-worker mode is silently demoted to 1 with a warning. */
static int effective_workers(const ts_config_t *cfg) {
    int n = cfg->workers;
#ifdef __linux__
    if (n <= 0) {
        long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        n = (ncpu > 0) ? (int)ncpu : 1;
    }
    if (n < 1) n = 1;
    return n;
#else
    if (n > 1) {
        LOG_WARN("multi-worker (-j %d) requires Linux SO_REUSEPORT load-balancing; "
                 "falling back to 1 worker on this platform", n);
    }
    return 1;
#endif
}

/* Create a listening socket bound to addr with SO_REUSEADDR and, if
 * multi_worker is set, SO_REUSEPORT. Returns the fd or -1 on error. */
static int make_listen_fd(const struct sockaddr *addr, socklen_t alen,
                         int multi_worker) {
    int fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        return -1;
    }
    /* Best-effort CLOEXEC (we don't exec, but children may.) */
    int fl = fcntl(fd, F_GETFD, 0);
    if (fl >= 0) (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC);

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
        LOG_WARN("SO_REUSEADDR: %s", strerror(errno));

    if (multi_worker) {
#ifdef SO_REUSEPORT
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0) {
            LOG_ERROR("SO_REUSEPORT: %s", strerror(errno));
            close(fd);
            return -1;
        }
#else
        LOG_ERROR("SO_REUSEPORT not supported by this kernel");
        close(fd);
        return -1;
#endif
    }

    if (bind(fd, addr, alen) < 0) {
        LOG_ERROR("bind %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* Run a single worker: create a fresh uv_loop, listen on its own
 * SO_REUSEPORT socket, install signal handlers, and run the loop until
 * shutdown. Returns 0 on clean exit. */
static int run_worker(ts_config_t *cfg, int multi_worker) {
    int r;

    g_config = cfg;

    if (multi_worker) {
        /* Each worker owns a fresh loop (the default loop in a child is
         * inherited from the parent and may have residual state). */
        loop = malloc(sizeof(*loop));
        if (!loop) { LOG_ERROR("oom for loop"); return -1; }
        if (uv_loop_init(loop) != 0) {
            LOG_ERROR("uv_loop_init failed");
            return -1;
        }
    } else {
        loop = uv_default_loop();
    }

    if (cfg->mode == 's' && cfg->config_file && g_routes.count == 0) {
        if (ts_routes_load(cfg->config_file, &g_routes) != 0) {
            LOG_ERROR("failed to load routes from %s", cfg->config_file);
            return -1;
        }
        LOG_INFO("loaded %d routes", g_routes.count);
    }

    uv_tcp_init(loop, &server_handle);

    struct sockaddr_storage addr;
    socklen_t alen;
    memset(&addr, 0, sizeof(addr));

    if (strchr(cfg->bind_addr, ':')) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
        r = uv_ip6_addr(cfg->bind_addr, cfg->port, addr6);
        alen = sizeof(*addr6);
    } else {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
        r = uv_ip4_addr(cfg->bind_addr, cfg->port, addr4);
        alen = sizeof(*addr4);
    }
    if (r != 0) {
        LOG_ERROR("invalid address '%s': %s", cfg->bind_addr, uv_strerror(r));
        return -1;
    }

    int fd = make_listen_fd((const struct sockaddr *)&addr, alen, multi_worker);
    if (fd < 0) return -1;

    r = uv_tcp_open(&server_handle, fd);
    if (r != 0) {
        LOG_ERROR("uv_tcp_open: %s", uv_strerror(r));
        close(fd);
        return -1;
    }

    r = uv_listen((uv_stream_t *)&server_handle, TS_DEFAULT_BACKLOG, on_connection);
    if (r != 0) {
        LOG_ERROR("listen error: %s", uv_strerror(r));
        return -1;
    }

    uv_signal_init(loop, &sigint_handle);
    uv_signal_init(loop, &sigterm_handle);
    uv_signal_start(&sigint_handle, on_signal, SIGINT);
    uv_signal_start(&sigterm_handle, on_signal, SIGTERM);

    LOG_INFO("worker pid=%d listening on %s:%d", (int)getpid(),
             cfg->bind_addr, cfg->port);

    uv_run(loop, UV_RUN_DEFAULT);

    if (cfg->mode == 's') ts_routes_free(&g_routes);
    uv_loop_close(loop);
    if (multi_worker) {
        free(loop);
        loop = NULL;
    }

    return 0;
}

/* PIDs of forked worker children (supervisor only). */
static pid_t *g_worker_pids;
static int    g_worker_count;

static void supervisor_forward(int signum) {
    for (int i = 0; i < g_worker_count; i++) {
        if (g_worker_pids[i] > 0)
            kill(g_worker_pids[i], signum);
    }
}

int ts_server_start(ts_config_t *cfg) {
    int n = effective_workers(cfg);

    LOG_INFO("tinyserve v%s starting (mode: %c, workers: %d)",
             TS_VERSION, cfg->mode, n);
    if (cfg->mode == 'f') LOG_INFO("serving files from: %s", cfg->root_dir);
    if (cfg->mode == 'p') LOG_INFO("forwarding to %s:%d",
                                   cfg->target_host, cfg->target_port);

    /* Single-worker fast path: no fork, no SO_REUSEPORT (which would
     * needlessly weaken kernel-level port-collision detection). */
    if (n == 1) {
        return run_worker(cfg, 0);
    }

    /* Multi-worker: load shared read-only state in the parent BEFORE
     * forking so each child inherits the same parsed routes. */
    if (cfg->mode == 's' && cfg->config_file) {
        if (ts_routes_load(cfg->config_file, &g_routes) != 0) {
            LOG_ERROR("failed to load routes from %s", cfg->config_file);
            return -1;
        }
        LOG_INFO("loaded %d routes (shared with workers)", g_routes.count);
    }

    g_worker_count = n;
    g_worker_pids  = calloc((size_t)n, sizeof(*g_worker_pids));
    if (!g_worker_pids) {
        LOG_ERROR("oom for worker pid table");
        return -1;
    }

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERROR("fork: %s", strerror(errno));
            /* Tear down whatever we already started. */
            supervisor_forward(SIGTERM);
            free(g_worker_pids);
            return -1;
        }
        if (pid == 0) {
            /* Child: detach from supervisor signals and run a worker.
             * If the supervisor dies, get SIGTERM (Linux only). */
#ifdef __linux__
            prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
            free(g_worker_pids);
            g_worker_pids = NULL;
            int rc = run_worker(cfg, 1);
            _exit(rc == 0 ? 0 : 1);
        }
        g_worker_pids[i] = pid;
    }

    /* Supervisor: forward signals to children, then reap. */
    struct sigaction sa = {0};
    sa.sa_handler = supervisor_forward;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    /* Reap any worker that exits unexpectedly so the kernel can free
     * its slot; log it but do NOT respawn (keeps semantics simple). */
    int alive = n;
    while (alive > 0) {
        int status;
        pid_t pid = wait(&status);
        if (pid < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; i++) {
            if (g_worker_pids[i] == pid) {
                g_worker_pids[i] = -1;
                alive--;
                if (WIFEXITED(status)) {
                    LOG_INFO("worker pid=%d exited (status=%d)",
                             (int)pid, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    LOG_INFO("worker pid=%d killed by signal %d",
                             (int)pid, WTERMSIG(status));
                }
                break;
            }
        }
    }

    free(g_worker_pids);
    g_worker_pids = NULL;
    if (cfg->mode == 's') ts_routes_free(&g_routes);
    return 0;
}
