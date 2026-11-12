#define _POSIX_C_SOURCE 200809L

#include "bench.h"
#include "sockets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The event-loop model serves at most SOCK_MAX_CONNS (32) connections at
 * a time, so both models are measured against the same cap. */
#define BENCH_MAX_CLIENTS 32
#define BENCH_MAX_REQUESTS 64
#define BENCH_MAX_PAYLOAD 256
#define BENCH_MAX_LATENCIES (BENCH_MAX_CLIENTS * BENCH_MAX_REQUESTS)

typedef struct {
    int  payload_len;
    long handled;
} bench_ctx;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Read exactly n bytes. A short read is normal (Stage 65.4), so this
 * loops; EOF before n bytes is the end of the conversation. */
static int read_exact(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = sock_read_once(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;   /* EOF before n bytes */
        got += (size_t)r;
    }
    return 0;
}

static int write_exact(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = sock_write_once(fd, p + sent, n - sent);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        sent += (size_t)w;
    }
    return 0;
}

static int send_frame(int fd, const void *payload, uint32_t len)
{
    char frame[4 + BENCH_MAX_PAYLOAD];
    int n = sock_frame_encode(frame, sizeof frame, payload, len);
    if (n < 0) return -1;
    return write_exact(fd, frame, (size_t)n);
}

static int recv_frame(int fd, void *payload, uint32_t cap, uint32_t *len_out)
{
    unsigned char hdr[4];
    uint32_t len;
    if (read_exact(fd, hdr, sizeof hdr) != 0) return -1;
    len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
          ((uint32_t)hdr[2] << 8) | (uint32_t)hdr[3];
    if (len > cap) return -1;
    if (len > 0 && read_exact(fd, payload, len) != 0) return -1;
    *len_out = len;
    return 0;
}

static void build_payload(char *buf, int len, int client, int req)
{
    int n = snprintf(buf, (size_t)len, "req:%03d:%03d", client, req);
    while (n < len && n > 0) buf[n++] = '.';
    if (n < len) buf[n] = '\0';
}

/* --- the fork model's handler: one child, one connection, raw frames --- */
static int fork_echo_handler(int conn_fd, int index, void *ctxp)
{
    bench_ctx *ctx = ctxp;
    char payload[BENCH_MAX_PAYLOAD];
    char frame[4 + BENCH_MAX_PAYLOAD];
    (void)index;
    for (;;) {
        uint32_t len = 0;
        if (recv_frame(conn_fd, payload, sizeof payload, &len) != 0) break;
        int n = sock_frame_encode(frame, sizeof frame, payload, len);
        if (n < 0) break;
        if (write_exact(conn_fd, frame, (size_t)n) != 0) break;
        ctx->handled++;
    }
    return 0;
}

/* --- the event model's handler: one process, many connections --- */
static int event_echo_handler(sock_conn *c, const char *msg, uint32_t len,
                              void *ctxp)
{
    bench_ctx *ctx = ctxp;
    sock_conn_queue_frame(c, msg, len);
    ctx->handled++;
    return 0;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* The client: open every connection FIRST, so the models really do have
 * to hold N conversations open at once, then walk them round-robin. */
static int run_client(const bench_config *cfg, uint16_t port, bench_result *out,
                      double *latencies, int *nlat)
{
    int fds[BENCH_MAX_CLIENTS];
    char payload[BENCH_MAX_PAYLOAD];
    char reply[BENCH_MAX_PAYLOAD];
    int opened = 0;
    long sent = 0, received = 0, completed = 0, mismatches = 0;

    for (int i = 0; i < cfg->clients; i++) {
        fds[i] = sock_connect_loopback(port);
        if (fds[i] < 0) break;
        opened++;
    }
    if (opened != cfg->clients) {
        for (int i = 0; i < opened; i++) sock_close(fds[i]);
        return -1;
    }

    double t0 = now_s();
    for (int r = 0; r < cfg->requests_each; r++) {
        for (int c = 0; c < opened; c++) {
            build_payload(payload, cfg->payload_len, c, r);
            double a = now_s();
            if (send_frame(fds[c], payload, (uint32_t)cfg->payload_len) != 0) goto done;
            sent += 4 + cfg->payload_len;
            uint32_t len = 0;
            if (recv_frame(fds[c], reply, sizeof reply, &len) != 0) goto done;
            received += 4 + (long)len;
            double b = now_s();
            if (latencies && *nlat < BENCH_MAX_LATENCIES) {
                latencies[(*nlat)++] = (b - a) * 1000.0;
            }
            if (len != (uint32_t)cfg->payload_len ||
                memcmp(reply, payload, len) != 0) {
                mismatches++;
            }
            completed++;
        }
    }

done:
    for (int i = 0; i < opened; i++) sock_close(fds[i]);
    out->requests_completed = completed;
    out->bytes_sent = sent;
    out->bytes_received = received;
    out->mismatches = mismatches;
    out->wall_s = now_s() - t0;
    out->throughput = out->wall_s > 0.0 ? (double)completed / out->wall_s : 0.0;
    return (completed == (long)opened * cfg->requests_each && mismatches == 0) ? 0 : -1;
}

const char *bench_model_name(bench_model m)
{
    return m == BENCH_MODEL_EVENT ? "event-loop" : "fork";
}

int bench_run(const bench_config *cfg, bench_result *out)
{
    double latencies[BENCH_MAX_LATENCIES];
    int nlat = 0;
    bench_ctx ctx;
    pid_t pid;
    int status = 0;

    if (cfg->clients < 1 || cfg->clients > BENCH_MAX_CLIENTS) return -1;
    if (cfg->requests_each < 1 || cfg->requests_each > BENCH_MAX_REQUESTS) return -1;
    if (cfg->payload_len < 8 || cfg->payload_len > BENCH_MAX_PAYLOAD) return -1;

    memset(out, 0, sizeof *out);
    out->model = cfg->model;
    ctx.payload_len = cfg->payload_len;
    ctx.handled = 0;

    if (cfg->model == BENCH_MODEL_FORK) {
        sock_server s;
        if (sock_server_start(&s, 0, 64) != 0) return -1;
        pid = fork();
        if (pid < 0) return -1;
        if (pid == 0) {
            int rc = sock_server_serve(&s, fork_echo_handler, &ctx, cfg->clients);
            _exit(rc == 0 ? 0 : 1);
        }
        if (run_client(cfg, s.listener.port, out, latencies, &nlat) != 0) {
            waitpid(pid, &status, 0);
            return -1;
        }
        if (waitpid(pid, &status, 0) != pid) return -1;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    } else {
        sock_mux m;
        if (sock_mux_start(&m, 0, 64) != 0) return -1;
        pid = fork();
        if (pid < 0) return -1;
        if (pid == 0) {
            long total = (long)cfg->clients * cfg->requests_each;
            for (;;) {
                sock_mux_accept(&m);
                sock_mux_poll(&m, event_echo_handler, &ctx, 20);
                if (ctx.handled >= total) {
                    sock_mux_poll(&m, event_echo_handler, &ctx, 20);
                    break;
                }
            }
            _exit(0);
        }
        if (run_client(cfg, m.listener.port, out, latencies, &nlat) != 0) {
            waitpid(pid, &status, 0);
            return -1;
        }
        if (waitpid(pid, &status, 0) != pid) return -1;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    }

    if (nlat > 0) {
        qsort(latencies, (size_t)nlat, sizeof latencies[0], cmp_double);
        out->min_ms = latencies[0];
        out->p50_ms = latencies[nlat / 2];
        out->p99_ms = latencies[(nlat * 99) / 100 < nlat ? (nlat * 99) / 100 : nlat - 1];
    }
    out->ok = 1;
    return 0;
}

int bench_compare(const bench_config *base, bench_result *fork_out,
                  bench_result *event_out)
{
    bench_config cfg = *base;
    cfg.model = BENCH_MODEL_FORK;
    if (bench_run(&cfg, fork_out) != 0) return -1;
    cfg.model = BENCH_MODEL_EVENT;
    if (bench_run(&cfg, event_out) != 0) return -1;
    return 0;
}
