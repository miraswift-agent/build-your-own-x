/*
 * Stage 65.4 tests: errors as protocol.
 *
 * The lesson: read() returning less than asked is normal, not an error.
 *
 * Stage 65.1 shipped sock_read_once / sock_write_once on purpose: one
 * syscall, a number back, and the caller is left to notice that a number
 * smaller than the one it asked for is neither a success nor a failure. A
 * short write in that API is a silent truncation -- the bytes are gone and
 * nothing says so.
 *
 * This stage turns the outcome into data. Every transfer answers with a
 * status (complete / partial / eof / interrupted / would-block / error), the
 * number of bytes actually moved, and -- when it really is an error -- the
 * errno value captured at the moment of failure rather than read out of a
 * global later.
 *
 * The tests here check the protocol, not the plumbing: a short read is
 * PARTIAL and not EOF, EOF is EOF and not a short read, EINTR is retryable
 * and not an error, EAGAIN is "not now" and not an error, and a real error
 * carries its errno as a value. The last test contrasts the new API with
 * the old one on the same socket, because the whole point of the stage is
 * that the old one could not tell you.
 *
 * Hermetic: socketpairs and loopback only. No network.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/sockets.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-52s", #name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("✓\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("✗ %s\n", msg); \
    } while (0)

/* A socketpair, plus the peer end. Hermetic by construction: no address, no
 * port, no kernel routing table. */
static int make_pair(int sv[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        exit(1);
    }
    return 0;
}

static void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        perror("fcntl O_NONBLOCK");
        exit(1);
    }
}

/* A small send buffer, so a big write cannot possibly fit. The kernel
 * doubles the value you ask for and has a floor, so this lands around 8KB. */
static void set_small_sndbuf(int fd)
{
    int buf = 4096;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf)) != 0) {
        perror("setsockopt SO_SNDBUF");
        exit(1);
    }
}

static volatile sig_atomic_t signal_seen = 0;

static void on_signal(int sig)
{
    (void)sig;
    signal_seen = 1;
}

/* ------------------------------------------------------------------ */

static void test_read_some_reports_partial_as_data(void)
{
    int sv[2];
    sock_io_result r;
    char buf[64];
    const char *payload = "0123456789";

    TEST(read_some_reports_partial_as_data);
    make_pair(sv);
    if (write(sv[0], payload, 10) != 10) {
        FAIL("setup write failed");
        close(sv[0]); close(sv[1]);
        return;
    }

    sock_read_some(sv[1], buf, sizeof(buf), &r);

    if (r.status == SOCK_IO_PARTIAL && r.transferred == 10 && r.err == 0 &&
        memcmp(buf, payload, 10) == 0) {
        PASS();
    } else {
        printf("✗ expected PARTIAL/10/0, got %s/%zu/%d\n",
               sock_io_status_name(r.status), r.transferred, r.err);
    }
    close(sv[0]); close(sv[1]);
}

static void test_read_some_reports_complete(void)
{
    int sv[2];
    sock_io_result r;
    char buf[64];
    const char *payload = "0123456789";

    TEST(read_some_reports_complete);
    make_pair(sv);
    if (write(sv[0], payload, 10) != 10) {
        FAIL("setup write failed");
        close(sv[0]); close(sv[1]);
        return;
    }

    sock_read_some(sv[1], buf, 10, &r);

    if (r.status == SOCK_IO_COMPLETE && r.transferred == 10 && r.err == 0) {
        PASS();
    } else {
        printf("✗ expected COMPLETE/10/0, got %s/%zu/%d\n",
               sock_io_status_name(r.status), r.transferred, r.err);
    }
    close(sv[0]); close(sv[1]);
}

static void test_read_some_reports_eof_not_short(void)
{
    int sv[2];
    sock_io_result r;
    char buf[64];

    TEST(read_some_reports_eof_not_short);
    make_pair(sv);
    close(sv[0]);  /* the peer is gone: the stream ends, it does not shorten */

    sock_read_some(sv[1], buf, sizeof(buf), &r);

    if (r.status == SOCK_IO_EOF && r.transferred == 0 && r.err == 0) {
        PASS();
    } else {
        printf("✗ expected EOF/0/0, got %s/%zu/%d\n",
               sock_io_status_name(r.status), r.transferred, r.err);
    }
    close(sv[1]);
}

static void test_read_exact_assembles_across_partials(void)
{
    int sv[2];
    sock_io_result r;
    char buf[16];
    pid_t child;

    TEST(read_exact_assembles_across_partials);
    make_pair(sv);

    /* The child writes four bytes, waits, then four more. A single read()
     * cannot get all eight: the loop is what makes the result complete. */
    child = fork();
    if (child == 0) {
        close(sv[1]);
        if (write(sv[0], "0123", 4) != 4) _exit(1);
        usleep(120000);
        if (write(sv[0], "4567", 4) != 4) _exit(1);
        close(sv[0]);
        _exit(0);
    }
    close(sv[0]);

    memset(buf, 0, sizeof(buf));
    sock_read_exact(sv[1], buf, 8, &r);

    if (r.status == SOCK_IO_COMPLETE && r.transferred == 8 &&
        memcmp(buf, "01234567", 8) == 0) {
        PASS();
    } else {
        printf("✗ expected COMPLETE/8 with payload, got %s/%zu\n",
               sock_io_status_name(r.status), r.transferred);
    }
    close(sv[1]);
    waitpid(child, NULL, 0);
}

static void test_read_some_reports_interrupted(void)
{
    int sv[2];
    sock_io_result r;
    char buf[64];
    struct sigaction sa;
    pid_t child;
    pid_t parent = getpid();

    TEST(read_some_reports_interrupted);
    make_pair(sv);

    /* No SA_RESTART: the read must come back with EINTR rather than being
     * silently restarted. */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sa.sa_flags = 0;  /* deliberately NOT SA_RESTART */
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) != 0) {
        FAIL("sigaction failed");
        close(sv[0]); close(sv[1]);
        return;
    }

    /* The child keeps signalling until the parent's read is interrupted, so
     * the test cannot lose a race. */
    child = fork();
    if (child == 0) {
        int i;
        close(sv[1]);
        usleep(150000);  /* let the parent reach the blocking read first */
        for (i = 0; i < 500; i++) {
            if (kill(parent, SIGUSR1) != 0) break;
            usleep(5000);
        }
        _exit(0);
    }
    close(sv[0]);

    sock_read_some(sv[1], buf, sizeof(buf), &r);

    if (r.status == SOCK_IO_INTERRUPTED && r.transferred == 0 && r.err == 0) {
        PASS();
    } else {
        printf("✗ expected INTERRUPTED/0/0, got %s/%zu/%d\n",
               sock_io_status_name(r.status), r.transferred, r.err);
    }
    close(sv[1]);
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
}

static void test_read_some_reports_would_block(void)
{
    int sv[2];
    sock_io_result r;
    char buf[64];

    TEST(read_some_reports_would_block);
    make_pair(sv);
    set_nonblock(sv[1]);  /* nothing to read, and no blocking either */

    sock_read_some(sv[1], buf, sizeof(buf), &r);

    /* "Not now" is not an error: err stays 0 and the status says why. */
    if (r.status == SOCK_IO_WOULD_BLOCK && r.transferred == 0 && r.err == 0) {
        PASS();
    } else {
        printf("✗ expected WOULD_BLOCK/0/0, got %s/%zu/%d\n",
               sock_io_status_name(r.status), r.transferred, r.err);
    }
    close(sv[0]); close(sv[1]);
}

static void test_read_some_reports_error_with_errno_as_data(void)
{
    int sv[2];
    sock_io_result r;
    char buf[64];

    TEST(read_some_reports_error_with_errno_as_data);
    make_pair(sv);
    close(sv[0]);
    close(sv[1]);  /* a closed descriptor: EBADF */

    sock_read_some(sv[1], buf, sizeof(buf), &r);

    /* The errno travels with the result. The caller does not have to read
     * the global errno before it does anything else. */
    if (r.status == SOCK_IO_ERROR && r.err == EBADF && r.transferred == 0) {
        PASS();
    } else {
        printf("✗ expected ERROR/EBADF, got %s/err=%d\n",
               sock_io_status_name(r.status), r.err);
    }
}

static void test_write_some_reports_complete(void)
{
    int sv[2];
    sock_io_result r;
    const char *payload = "0123456789";

    TEST(write_some_reports_complete);
    make_pair(sv);

    sock_write_some(sv[0], payload, 10, &r);

    if (r.status == SOCK_IO_COMPLETE && r.transferred == 10 && r.err == 0) {
        PASS();
    } else {
        printf("✗ expected COMPLETE/10/0, got %s/%zu/%d\n",
               sock_io_status_name(r.status), r.transferred, r.err);
    }
    close(sv[0]); close(sv[1]);
}

static void test_write_some_reports_partial_not_truncation(void)
{
    int sv[2];
    sock_io_result r;
    char *big;
    const size_t big_len = 1024 * 1024;

    TEST(write_some_reports_partial_not_truncation);
    make_pair(sv);
    set_nonblock(sv[0]);
    set_small_sndbuf(sv[0]);

    big = malloc(big_len);
    if (big == NULL) { FAIL("malloc failed"); close(sv[0]); close(sv[1]); return; }
    memset(big, 'x', big_len);

    sock_write_some(sv[0], big, big_len, &r);

    /* Either a partial transfer or "would block" -- what must NOT happen is
     * COMPLETE, because nothing that big fit. */
    if (r.status != SOCK_IO_COMPLETE && r.transferred < big_len) {
        PASS();
    } else {
        printf("✗ expected a short transfer, got %s/%zu of %zu\n",
               sock_io_status_name(r.status), r.transferred, big_len);
    }
    free(big);
    close(sv[0]); close(sv[1]);
}

static void test_write_exact_reports_progress_when_blocked(void)
{
    int sv[2];
    sock_io_result r;
    char *big;
    const size_t big_len = 1024 * 1024;

    TEST(write_exact_reports_progress_when_blocked);
    make_pair(sv);
    set_nonblock(sv[0]);
    set_small_sndbuf(sv[0]);

    big = malloc(big_len);
    if (big == NULL) { FAIL("malloc failed"); close(sv[0]); close(sv[1]); return; }
    memset(big, 'y', big_len);

    sock_write_exact(sv[0], big, big_len, &r);

    /* The write cannot finish. The result says so AND says how far it got:
     * the caller knows exactly how many bytes are already on the wire. */
    if (r.status == SOCK_IO_WOULD_BLOCK && r.transferred > 0 &&
        r.transferred < big_len && r.err == 0) {
        PASS();
    } else {
        printf("✗ expected WOULD_BLOCK with 0<transferred<%zu, got %s/%zu\n",
               big_len, sock_io_status_name(r.status), r.transferred);
    }
    free(big);
    close(sv[0]); close(sv[1]);
}

static void test_write_exact_completes_on_blocking_socket(void)
{
    int sv[2];
    sock_io_result r;
    char payload[4096];

    TEST(write_exact_completes_on_blocking_socket);
    make_pair(sv);
    memset(payload, 'z', sizeof(payload));

    sock_write_exact(sv[0], payload, sizeof(payload), &r);

    if (r.status == SOCK_IO_COMPLETE && r.transferred == sizeof(payload) &&
        r.err == 0) {
        PASS();
    } else {
        printf("✗ expected COMPLETE/%zu, got %s/%zu\n",
               sizeof(payload), sock_io_status_name(r.status), r.transferred);
    }
    close(sv[0]); close(sv[1]);
}

static void test_naive_write_once_truncates_silently(void)
{
    int sv[2];
    sock_io_result r;
    char *big;
    const size_t big_len = 1024 * 1024;
    ssize_t naive;

    TEST(naive_write_once_truncates_silently);
    make_pair(sv);
    set_nonblock(sv[0]);
    set_small_sndbuf(sv[0]);

    big = malloc(big_len);
    if (big == NULL) { FAIL("malloc failed"); close(sv[0]); close(sv[1]); return; }
    memset(big, 'w', big_len);

    /* The old API: a number. A caller that does not compare it against
     * big_len has lost the tail of the buffer and cannot tell. */
    naive = sock_write_once(sv[0], big, big_len);

    /* The new API: the same call, with the truncation made visible. */
    sock_write_some(sv[0], big, big_len, &r);

    if (naive >= 0 && (size_t)naive < big_len &&
        r.status != SOCK_IO_COMPLETE && r.transferred < big_len) {
        PASS();
    } else {
        printf("✗ naive=%zd new=%s/%zu of %zu\n", naive,
               sock_io_status_name(r.status), r.transferred, big_len);
    }
    free(big);
    close(sv[0]); close(sv[1]);
}

static void test_status_names_are_distinct(void)
{
    const sock_io_status all[] = {
        SOCK_IO_COMPLETE, SOCK_IO_PARTIAL, SOCK_IO_EOF,
        SOCK_IO_INTERRUPTED, SOCK_IO_WOULD_BLOCK, SOCK_IO_ERROR
    };
    const size_t n = sizeof(all) / sizeof(all[0]);
    size_t i, j;
    int distinct = 1;

    TEST(status_names_are_distinct);
    for (i = 0; i < n; i++) {
        const char *name = sock_io_status_name(all[i]);
        if (name == NULL || name[0] == '\0') { distinct = 0; break; }
        for (j = i + 1; j < n; j++) {
            if (strcmp(name, sock_io_status_name(all[j])) == 0) {
                distinct = 0;
            }
        }
    }
    if (distinct) {
        PASS();
    } else {
        FAIL("status names are not distinct");
    }
}

int main(void)
{
    printf("Stage 65.4 — errors as protocol (short reads, short writes, errno as data)\n\n");

    test_read_some_reports_partial_as_data();
    test_read_some_reports_complete();
    test_read_some_reports_eof_not_short();
    test_read_exact_assembles_across_partials();
    test_read_some_reports_interrupted();
    test_read_some_reports_would_block();
    test_read_some_reports_error_with_errno_as_data();
    test_write_some_reports_complete();
    test_write_some_reports_partial_not_truncation();
    test_write_exact_reports_progress_when_blocked();
    test_write_exact_completes_on_blocking_socket();
    test_naive_write_once_truncates_silently();
    test_status_names_are_distinct();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
