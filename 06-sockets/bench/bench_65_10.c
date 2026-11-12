/* Stage 65.10 -- the measurement itself.
 *
 * Runs the same workload against both concurrency models and prints the
 * comparison. The numbers are what the close-out quotes; the tests
 * assert the accounting, not the ranking (a ranking is a property of
 * the machine, not of the code). */
#include "../src/bench.h"

#include <stdio.h>
#include <stdlib.h>

static void row(const char *label, const bench_result *r)
{
    printf("%-12s %8ld %10.1f %10.3f %10.3f %10.3f %10.3f\n", label,
           r->requests_completed, r->wall_s * 1000.0, r->throughput,
           r->min_ms, r->p50_ms, r->p99_ms);
}

int main(int argc, char **argv)
{
    int clients = argc > 1 ? atoi(argv[1]) : 16;
    int requests = argc > 2 ? atoi(argv[2]) : 20;
    bench_config base;
    bench_result fr, ev;

    base.clients = clients;
    base.requests_each = requests;
    base.payload_len = 64;
    base.model = BENCH_MODEL_FORK;

    if (bench_compare(&base, &fr, &ev) != 0) {
        fprintf(stderr, "bench: a run did not complete cleanly\n");
        return 1;
    }

    printf("Stage 65.10 -- two concurrency models on one server\n");
    printf("workload: %d concurrent connections x %d requests, %d-byte payloads\n\n",
           clients, requests, base.payload_len);
    printf("%-12s %8s %10s %10s %10s %10s %10s\n", "model", "done", "wall_ms",
           "req/s", "min_ms", "p50_ms", "p99_ms");
    row(bench_model_name(BENCH_MODEL_FORK), &fr);
    row(bench_model_name(BENCH_MODEL_EVENT), &ev);
    return 0;
}
