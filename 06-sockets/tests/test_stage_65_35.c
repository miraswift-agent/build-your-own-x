/*
 * Stage 65.35 -- the client's half of Expect.
 *
 * Stage 65.33 gave the SERVER the 100 Continue; the client could read
 * a 1xx (Stage 65.31) but could not ASK. This stage gives the client
 * the asking:
 *
 *   - the head builder, split, so the head can go out alone;
 *   - a bounded wait for the server's answer (RFC 9110 10.1.1: a
 *     client SHOULD NOT wait for an indefinite period);
 *   - the body goes only if the server wants it.
 *
 * The clock is part of the stage, not a follow-up: Stage 65.33's
 * discrimination proof found that a missing interim response does not
 * fail fast, it DEADLOCKS -- the client waits for the 100 and the
 * server waits for the body. An unbounded wait would reproduce that
 * against every server that does not answer.
 *
 * The oracles: our own server (the 100 arrives and the body is served)
 * and a raw peer (the sharpest one -- a peer that answers 417 without
 * reading a body, and a peer that answers nothing at all).
 */
/* _DEFAULT_SOURCE exposes clock_gettime and CLOCK_MONOTONIC under
 * -std=c11. */
#define _DEFAULT_SOURCE

#include "../src/http.h"
#include "../src/http_client.h"
#include "../src/sockets.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static const char *current_test = "(none)";

#define TEST(name) do { tests_run++; current_test = name; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

static void on_alarm(int sig)
{
    static const char msg[] = "FAIL: timed out -- see the test above\n";
    (void)sig;
    (void)write(2, msg, sizeof(msg) - 1);
    (void)write(2, current_test, strlen(current_test));
    (void)write(2, "\n", 1);
    _exit(1);
}

static const char our_body[] = "hello from our server\n";

/* Our own server, one connection. */
static uint16_t spawn_our_server(pid_t *pid_out)
{
    sock_listener l;
    pid_t pid;

    if (sock_listen_loopback(&l, 0, 8) != 0) {
        perror("sock_listen_loopback");
        exit(1);
    }
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        int fd = sock_accept(&l);
        if (fd >= 0) {
            http_server_stats st;
            http_serve_connection(fd, our_body, &st);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

static void reap(pid_t pid)
{
    int st;
    if (pid > 0) {
        waitpid(pid, &st, 0);
    }
}

/* A connected pair of sockets: the client speaks on one end, the peer
 * on the other. The peer is the sharpest oracle -- it can answer
 * exactly what the stage needs answered (or nothing at all). */
static void socket_pair(int sv[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        exit(1);
    }
}

/* A client connection on one end of the pair. */
static void client_on(int fd, http_client_connection *c)
{
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->port = 0;
    memcpy(c->host, "127.0.0.1", sizeof("127.0.0.1"));
    http_response_parser_init(&c->parser);
}

/* Where the head ends: the first empty line. */
static long head_end(const char *buf, size_t len)
{
    size_t i;

    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return (long)(i + 4);
        }
    }
    return -1;
}

/* Read until the head is complete and `want` more bytes have arrived. */
static size_t read_head_and_body(int fd, char *buf, size_t cap, size_t want)
{
    size_t total = 0;

    while (total < cap) {
        ssize_t n = read(fd, buf + total, cap - total);
        long end;

        if (n <= 0) {
            break;
        }
        total += (size_t)n;
        end = head_end(buf, total);
        if (end >= 0 && total >= (size_t)end + want) {
            break;
        }
    }
    return total;
}

static void test_the_head_carries_the_expectation(void)
{
    int sv[2];
    http_client_connection c;
    char buf[1024];
    ssize_t n;

    TEST("the head carries the expectation, and no body");
    socket_pair(sv);
    client_on(sv[0], &c);
    if (http_client_connection_write_head(&c, "POST", "/echo", 5, 1, 1) != 0) {
        FAIL("the head was refused");
        return;
    }
    n = read(sv[1], buf, sizeof(buf) - 1);
    if (n <= 0) {
        FAIL("the peer read nothing");
        return;
    }
    buf[n] = '\0';
    if (strstr(buf, "Expect: 100-continue\r\n") == NULL) {
        FAIL("the expectation is not on the wire");
        return;
    }
    if (strstr(buf, "Content-Length: 5\r\n") == NULL) {
        FAIL("the length is not on the wire");
        return;
    }
    if (head_end(buf, (size_t)n) < 0) {
        FAIL("the head is not terminated");
        return;
    }
    if (strstr(buf, "hello") != NULL) {
        FAIL("the body was on the wire");
        return;
    }
    PASS();
}

static void test_our_server_says_continue(void)
{
    pid_t pid;
    uint16_t port;
    http_response r;
    const char body[] = "alpha";

    TEST("our server says continue, and the body is served");
    port = spawn_our_server(&pid);
    if (http_client_request_expect("127.0.0.1", port, "POST", "/echo",
                                   body, strlen(body), 1000, &r) != 0) {
        FAIL("the expect request failed");
        reap(pid);
        return;
    }
    reap(pid);
    if (r.status != 200) {
        FAIL("the answer was not a 200");
        return;
    }
    if (r.body_len != strlen(body) || memcmp(r.body, body, r.body_len) != 0) {
        FAIL("the echo did not come back");
        return;
    }
    PASS();
}

static void test_a_final_response_before_the_body_sends_no_body(void)
{
    int sv[2];
    http_client_connection c;
    http_response msg;
    char buf[256];
    ssize_t n;

    TEST("a final response before the body: no body is sent");
    socket_pair(sv);
    client_on(sv[0], &c);
    if (http_client_connection_write_head(&c, "POST", "/echo", 5, 1, 1) != 0) {
        FAIL("the head was refused");
        return;
    }
    {
        const char ans[] = "HTTP/1.1 417 Expectation Failed\r\nContent-Length: 0\r\n\r\n";
        if (write(sv[1], ans, sizeof(ans) - 1) < 0) {
            FAIL("the peer could not answer");
            return;
        }
    }
    if (http_client_connection_expect_continue(&c, 1000, &msg) != 0) {
        FAIL("the wait did not return the final response");
        return;
    }
    if (msg.status != 417) {
        FAIL("the final response was not the 417");
        return;
    }
    /* The HEAD is on the wire already -- it went out before the wait.
     * Drain it, and THEN look: what must not follow is the BODY. */
    {
        size_t total = 0;
        long end = -1;

        while (total < sizeof(buf) - 1) {
            ssize_t got = recv(sv[1], buf + total, sizeof(buf) - 1 - total, 0);
            if (got <= 0) {
                break;
            }
            total += (size_t)got;
            buf[total] = '\0';
            end = head_end(buf, total);
            if (end >= 0) {
                break;
            }
        }
        if (end < 0) {
            FAIL("the head never arrived");
            return;
        }
    }
    /* Nothing more may go on the wire: the peer has answered, and the
     * content is not wanted. */
    n = recv(sv[1], buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0) {
        FAIL("a body was sent after the final response");
        return;
    }
    if (n == 0) {
        FAIL("the connection closed instead of staying open");
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        PASS();
        return;
    }
    FAIL("the peer read failed");
}

static void test_no_answer_means_the_body_goes_anyway(void)
{
    int sv[2];
    http_client_connection c;
    http_response msg;
    char buf[1024];
    size_t total;
    long end;

    TEST("no answer at all: the body goes anyway");
    socket_pair(sv);
    client_on(sv[0], &c);
    if (http_client_connection_write_head(&c, "POST", "/echo", 5, 1, 1) != 0) {
        FAIL("the head was refused");
        return;
    }
    errno = 0;
    if (http_client_connection_expect_continue(&c, 200, &msg) != -1 ||
        errno != ETIMEDOUT) {
        FAIL("the wait did not time out");
        return;
    }
    if (http_client_connection_write_body(&c, "hello", 5) != 0) {
        FAIL("the body was refused");
        return;
    }
    total = read_head_and_body(sv[1], buf, sizeof(buf), 5);
    end = head_end(buf, total);
    if (end < 0) {
        FAIL("the head never arrived");
        return;
    }
    if (total < (size_t)end + 5 || memcmp(buf + end, "hello", 5) != 0) {
        FAIL("the body did not go after the head");
        return;
    }
    PASS();
}

static void test_expect_with_no_content_is_refused(void)
{
    int sv[2];
    http_client_connection c;
    char buf[64];
    ssize_t n;

    TEST("expect with no content is refused, and nothing goes out");
    socket_pair(sv);
    client_on(sv[0], &c);
    errno = 0;
    if (http_client_connection_write_head(&c, "POST", "/echo", 0, 1, 1) != -1 ||
        errno != EINVAL) {
        FAIL("the expectation without content was accepted");
        return;
    }
    errno = 0;
    if (http_client_request_expect("127.0.0.1", 1, "POST", "/echo", NULL, 0,
                                   100, NULL) != -1 && errno != EINVAL) {
        FAIL("the one-shot accepted a request with no content");
        return;
    }
    n = recv(sv[1], buf, sizeof(buf), MSG_DONTWAIT);
    if (n >= 0) {
        FAIL("something went on the wire");
        return;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        FAIL("the peer read failed");
        return;
    }
    PASS();
}

static void test_the_interim_does_not_advance_the_queue(void)
{
    int sv[2];
    http_client_connection c;
    http_response msg;

    TEST("the interim does not advance the in-flight queue");
    socket_pair(sv);
    client_on(sv[0], &c);
    if (http_client_connection_write_head(&c, "POST", "/echo", 5, 1, 1) != 0) {
        FAIL("the head was refused");
        return;
    }
    if (c.n_inflight != 1) {
        FAIL("the head did not enter the queue");
        return;
    }
    {
        const char interim[] = "HTTP/1.1 100 Continue\r\n\r\n";
        const char final[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";

        if (write(sv[1], interim, sizeof(interim) - 1) < 0) {
            FAIL("the peer could not send the interim");
            return;
        }
        if (http_client_connection_expect_continue(&c, 1000, &msg) != 1) {
            FAIL("the interim was not returned");
            return;
        }
        if (msg.status != 100) {
            FAIL("the interim was not the 100");
            return;
        }
        if (c.n_inflight != 1) {
            FAIL("the interim advanced the queue");
            return;
        }
        if (write(sv[1], final, sizeof(final) - 1) < 0) {
            FAIL("the peer could not send the final response");
            return;
        }
        if (http_client_connection_read_response(&c, &msg) != 0) {
            FAIL("the final response failed");
            return;
        }
    }
    if (c.n_inflight != 0) {
        FAIL("the final response did not advance the queue");
        return;
    }
    PASS();
}

/* Stage 65.31's rule, carried across the expect split: the peer sends
 * the 100 and then closes without the final response. The final
 * response never came, so the request is INCOMPLETE -- not EPROTO
 * ("there was no response"): there was one, and it was not the
 * answer. The count of the interims has to survive the two calls, so
 * it lives in the connection. */
static void test_the_peer_closes_after_the_interim(void)
{
    int sv[2];
    http_client_connection c;
    http_response msg;

    TEST("the peer closes after the 100: the reason is incomplete");
    socket_pair(sv);
    client_on(sv[0], &c);
    if (http_client_connection_write_head(&c, "POST", "/echo", 5, 1, 1) != 0) {
        FAIL("the head was refused");
        return;
    }
    {
        const char interim[] = "HTTP/1.1 100 Continue\r\n\r\n";

        if (write(sv[1], interim, sizeof(interim) - 1) < 0) {
            FAIL("the peer could not send the interim");
            return;
        }
    }
    if (http_client_connection_expect_continue(&c, 1000, &msg) != 1) {
        FAIL("the interim was not returned");
        return;
    }
    /* The peer hangs up now: the final response never arrives.
     *
     * The head is drained FIRST: a socket closed with unread data in
     * its receive buffer sends a RESET, not a FIN, and a reset is a
     * different event (ECONNRESET, "the connection was aborted")
     * from a hangup ("the peer closed cleanly"). The rule under test
     * is about the hangup. */
    {
        char drain[512];
        size_t total = 0;
        long end = -1;

        while (total < sizeof(drain) - 1) {
            ssize_t got = recv(sv[1], drain + total, sizeof(drain) - 1 - total, 0);
            if (got <= 0) {
                break;
            }
            total += (size_t)got;
            drain[total] = '\0';
            end = head_end(drain, total);
            if (end >= 0) {
                break;
            }
        }
        if (end < 0) {
            FAIL("the head never arrived");
            return;
        }
    }
    close(sv[1]);
    errno = 0;
    if (http_client_connection_read_response(&c, &msg) != -1 ||
        errno != HTTP_REASON_INCOMPLETE) {
        FAIL("the closed connection was not reported as incomplete");
        return;
    }
    if (c.fd != -1) {
        FAIL("the connection was not closed");
        return;
    }
    PASS();
}

static void test_the_wait_is_bounded(void)
{    int sv[2];
    http_client_connection c;
    http_response msg;
    struct timespec t0;
    struct timespec t1;
    long ms;

    TEST("the wait is bounded -- the regression guard");
    socket_pair(sv);
    client_on(sv[0], &c);
    if (http_client_connection_write_head(&c, "POST", "/echo", 5, 1, 1) != 0) {
        FAIL("the head was refused");
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    errno = 0;
    if (http_client_connection_expect_continue(&c, 300, &msg) != -1 ||
        errno != ETIMEDOUT) {
        FAIL("the wait did not time out");
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (long)(t1.tv_sec - t0.tv_sec) * 1000L +
         (long)(t1.tv_nsec - t0.tv_nsec) / 1000000L;
    if (ms < 250 || ms > 2000) {
        FAIL("the wait was not about the timeout asked for");
        return;
    }
    PASS();
}

static void test_the_head_builder_and_the_request_agree(void)
{
    char head[1024];
    char full[1024];
    int hn;
    int fn;

    TEST("the head builder and the request builder agree");
    hn = http_client_request_build_head(head, sizeof(head), "POST", "/echo",
                                        "127.0.0.1", 5, 1, NULL, NULL);
    fn = http_client_request_build_with_header(full, sizeof(full), "POST", "/echo",
                                                "127.0.0.1", "hello", 5, 1,
                                                NULL, NULL);
    if (hn < 0 || fn < 0) {
        FAIL("a build failed");
        return;
    }
    if (fn != hn + 5) {
        FAIL("the lengths disagree");
        return;
    }
    if (memcmp(head, full, (size_t)hn) != 0) {
        FAIL("the head bytes differ");
        return;
    }
    if (memcmp(full + hn, "hello", 5) != 0) {
        FAIL("the body is not after the head");
        return;
    }
    PASS();
}

int main(void)
{
    signal(SIGALRM, on_alarm);
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(60);

    printf("Stage 65.35 -- the client's half of Expect\n");

    test_the_head_carries_the_expectation();
    test_our_server_says_continue();
    test_a_final_response_before_the_body_sends_no_body();
    test_no_answer_means_the_body_goes_anyway();
    test_expect_with_no_content_is_refused();
    test_the_interim_does_not_advance_the_queue();
    test_the_peer_closes_after_the_interim();
    test_the_wait_is_bounded();
    test_the_head_builder_and_the_request_agree();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
