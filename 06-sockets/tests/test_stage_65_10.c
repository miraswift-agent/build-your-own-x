/* Stage 65.10 -- two concurrency models on one server, measured.
 *
 * These tests assert the ACCOUNTING, never the ranking. "fork is faster"
 * is a fact about this machine on this evening; "every request was
 * answered exactly once, with the right bytes, and the bytes add up" is
 * a fact about the code. Only the second kind belongs in a test. */
#include "../src/bench.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

static void TEST(const char *name)
{
    tests_run++;
    printf("  %-56s", name);
    fflush(stdout);
}

static void PASS(void)
{
    tests_passed++;
    printf("OK\n");
}

static void FAIL(const char *why)
{
    printf("FAIL: %s\n", why);
}

static void check_accounting(const bench_result *r, const bench_config *cfg,
                             const char *model)
{
    long expect = (long)cfg->clients * cfg->requests_each;
    char buf[160];

    if (r->requests_completed != expect) {
        snprintf(buf, sizeof buf, "%s completed %ld of %ld requests", model,
                 r->requests_completed, expect);
        FAIL(buf);
        return;
    }
    if (r->mismatches != 0) {
        snprintf(buf, sizeof buf, "%s saw %ld mismatched replies", model,
                 r->mismatches);
        FAIL(buf);
        return;
    }
    long want = expect * (4 + cfg->payload_len);
    if (r->bytes_sent != want || r->bytes_received != want) {
        snprintf(buf, sizeof buf, "%s byte accounting off (sent %ld recv %ld want %ld)",
                 model, r->bytes_sent, r->bytes_received, want);
        FAIL(buf);
        return;
    }
    PASS();
}

int main(void)
{
    bench_config cfg;
    bench_result fr, ev;

    printf("Stage 65.10 tests: two concurrency models on one server\n\n");

    cfg.clients = 4;
    cfg.requests_each = 3;
    cfg.payload_len = 64;
    cfg.model = BENCH_MODEL_FORK;

    TEST("the fork model answers every request exactly once");
    if (bench_run(&cfg, &fr) != 0) {
        FAIL("the fork run did not complete");
    } else {
        check_accounting(&fr, &cfg, "fork");
    }

    TEST("the event model answers every request exactly once");
    if (bench_run(&cfg, &ev) != 0) {
        FAIL("the event run did not complete");
    } else {
        check_accounting(&ev, &cfg, "event");
    }

    TEST("both models serve the same workload");
    if (fr.requests_completed != ev.requests_completed) {
        FAIL("the two models completed different numbers of requests");
    } else {
        PASS();
    }

    TEST("the fork model leaves no children unreaped");
    if (fr.ok != 1) {
        FAIL("the fork server child did not exit cleanly");
    } else {
        PASS();
    }

    TEST("the event model leaves no children unreaped");
    if (ev.ok != 1) {
        FAIL("the event server child did not exit cleanly");
    } else {
        PASS();
    }

    TEST("both models report a positive throughput");
    if (fr.throughput <= 0.0 || ev.throughput <= 0.0) {
        FAIL("a model reported zero throughput");
    } else {
        PASS();
    }

    TEST("latency percentiles are ordered (min <= p50 <= p99)");
    if (!(fr.min_ms <= fr.p50_ms && fr.p50_ms <= fr.p99_ms &&
          ev.min_ms <= ev.p50_ms && ev.p50_ms <= ev.p99_ms)) {
        FAIL("the percentiles are not ordered");
    } else {
        PASS();
    }

    TEST("a comparison run fills in both models");
    if (bench_compare(&cfg, &fr, &ev) != 0) {
        FAIL("the comparison did not complete");
    } else if (fr.ok != 1 || ev.ok != 1) {
        FAIL("a comparison run did not complete cleanly");
    } else {
        PASS();
    }

    TEST("a zero-client workload is refused, not measured");
    {
        bench_config bad = cfg;
        bench_result junk;
        bad.clients = 0;
        if (bench_run(&bad, &junk) == 0) {
            FAIL("a workload with no clients was accepted");
        } else {
            PASS();
        }
    }

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
