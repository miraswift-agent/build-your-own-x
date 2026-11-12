#ifndef MIRA_BENCH_H
#define MIRA_BENCH_H

/* Stage 65.10 -- two concurrency models on one server, measured.
 *
 * Stage 65.3 built the fork() model: one process accepts, one child per
 * connection. Stage 65.5 built the event-loop model: one process, many
 * connections, select() says which ones are ready. Both are in this
 * component. Both work. The question this stage answers is the one the
 * design doc has always asked: *the tradeoff is throughput and latency,
 * not taste* -- so MEASURE it, on the same machine, with the same
 * workload, with the same wire protocol.
 *
 * The wire protocol is deliberately the length-prefixed frame from
 * Stage 65.5 for BOTH models. If one model spoke a different protocol,
 * the comparison would be measuring the protocol, not the concurrency.
 */

typedef enum {
    BENCH_MODEL_FORK  = 0,   /* one process accepts, one child per connection */
    BENCH_MODEL_EVENT = 1    /* one process, many connections, select() */
} bench_model;

typedef struct {
    int         clients;      /* concurrent connections */
    int         requests_each; /* requests per connection */
    int         payload_len;  /* bytes in a request payload */
    bench_model model;
} bench_config;

typedef struct {
    bench_model model;
    long   requests_completed; /* responses received */
    long   bytes_sent;
    long   bytes_received;
    long   mismatches;         /* replies that did not match their request */
    double wall_s;
    double throughput;         /* requests per second */
    double min_ms;
    double p50_ms;
    double p99_ms;
    int    ok;                 /* the run completed and every reply matched */
} bench_result;

const char *bench_model_name(bench_model m);

/* Run one model against one workload. Returns 0 on success. */
int bench_run(const bench_config *cfg, bench_result *out);

/* Run both models against the SAME workload and fill in both results.
 * Returns 0 on success. */
int bench_compare(const bench_config *base, bench_result *fork_out,
                  bench_result *event_out);

#endif /* MIRA_BENCH_H */
