/*
 * Stage 65.15b -- the client pipelines.
 *
 * Stage 65.14b gave the client a connection: it owns a socket, so a
 * second request can be written on it after the first response has been
 * read. That is one turn at a time. Pipelining is the other shape: two
 * requests go out BEFORE either response comes back.
 *
 * The claim under test is on the wire:
 *
 *   both requests leave before the peer answers either, and the
 *   responses come back in the order the requests were written.
 *
 * The first claim is proved by a peer that refuses to answer until it
 * has seen BOTH requests. A client that waits for the first response
 * before writing the second request would wait forever -- so the test
 * carries an alarm, and a hang becomes a failure with a message
 * instead of a hung test binary.
 *
 * The second claim is proved twice: against our own server (the two
 * routes answer with different bodies, so a swapped pair is visible),
 * and against Node, whose captured pipelined response NAMES THE PATH
 * in each body -- "body for /a" and "body for /b" -- so the order is
 * visible in the bytes themselves.
 */

#include "../src/http_client.h"
#include "../src/sockets.h"
#include "node_capture.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; current_test = name; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

static const char our_body[] = "hello from our server\n";
static const char our_stream_body[] = "Hello, chunked world!\n";

/* A hang is a poor failure mode. If the client waits for a response the
 * peer will not send until it has seen the second request, this fires
 * and the test fails with a reason. */
static const char *current_test = "(none)";

static void on_alarm(int sig)
{
    static const char msg[] = "FAIL: timed out -- see the test above\n";
    (void)sig;
    (void)write(2, msg, sizeof(msg) - 1);
    (void)write(2, current_test, strlen(current_test));
    (void)write(2, "\n", 1);
    _exit(1);
}

/* What the peer has seen so far. The count ACCUMULATES across calls:
 * a peer that answered the first request and is now waiting for the
 * second must not restart its count. */
typedef struct {
    char   acc[8192];
    size_t len;
    int    seen;
} req_log;

static int count_gets(const char *buf, size_t len)
{
    int n = 0;
    size_t i;

    for (i = 0; i + 4 <= len; i++) {
        if (memcmp(buf + i, "GET ", 4) == 0) {
            n++;
        }
    }
    return n;
}

/* Read until `want` request lines have arrived in total. Returns how
 * many were seen. */
static int read_until_gets(int fd, int want, req_log *log)
{
    while (log->seen < want) {
        ssize_t n = read(fd, log->acc + log->len, sizeof(log->acc) - log->len - 1);
        if (n <= 0) {
            return log->seen;
        }
        log->len += (size_t)n;
        log->acc[log->len] = '\0';
        log->seen = count_gets(log->acc, log->len);
    }
    return log->seen;
}

static int write_all(int fd, const unsigned char *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n <= 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

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

/* The pipelining peer: it answers NOTHING until it has seen both
 * requests. */
static uint16_t spawn_pipeline_peer(pid_t *pid_out)
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
            req_log log;
            memset(&log, 0, sizeof(log));
            if (read_until_gets(fd, 2, &log) == 2) {
                write_all(fd, node_pipelined, NODE_PIPELINED_LEN);
            }
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

/* Node's captured pipelined response, replayed on one socket. */
static uint16_t spawn_node_peer(pid_t *pid_out)
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
            req_log log;
            memset(&log, 0, sizeof(log));
            read_until_gets(fd, 2, &log);
            write_all(fd, node_pipelined, NODE_PIPELINED_LEN);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

/* A peer that answers the first request and then hangs up: the second
 * response never comes. */
static uint16_t spawn_one_and_close_peer(pid_t *pid_out)
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
            req_log log;
            memset(&log, 0, sizeof(log));
            if (read_until_gets(fd, 1, &log) >= 1) {
                write_all(fd, node_pipelined, NODE_PIPELINED_FIRST_LEN);
                /* Drain the second request before hanging up: a close
                 * with an unread request on the socket is an RST, and an
                 * RST throws away the bytes we just wrote. */
                read_until_gets(fd, 2, &log);
            }
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

static void test_the_write_and_the_read_are_separate(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;

    TEST("the write and the read are separate");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "GET", "/", NULL, 0) != 0) {
        FAIL("the write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r1) != 0) {
        FAIL("the read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r1.body_len != sizeof(our_body) - 1 || memcmp(r1.body, our_body, r1.body_len) != 0) {
        FAIL("the body is wrong");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_two_requests_go_out_before_either_response(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;
    http_response r2;

    TEST("two requests go out before either response");
    port = spawn_pipeline_peer(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    /* The peer answers nothing until it has seen both requests, so the
     * reads below can only succeed if both writes went out first. */
    alarm(10);
    if (http_client_connection_write_request(&c, "GET", "/a", NULL, 0) != 0) {
        FAIL("the first write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "GET", "/b", NULL, 0) != 0) {
        FAIL("the second write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r1) != 0) {
        FAIL("the first read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r2) != 0) {
        FAIL("the second read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    alarm(0);
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_responses_come_back_in_order(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;
    http_response r2;
    http_response r3;

    TEST("the responses come back in the order written");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    /* Two different routes, so a swapped pair is visible: "/" answers
     * with the plain body, "/stream" with the chunked one. */
    if (http_client_connection_write_request(&c, "GET", "/", NULL, 0) != 0 ||
        http_client_connection_write_request(&c, "GET", "/stream", NULL, 0) != 0) {
        FAIL("a write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r1) != 0) {
        FAIL("the first read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r2) != 0) {
        FAIL("the second read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r1.body_len != sizeof(our_body) - 1 || memcmp(r1.body, our_body, r1.body_len) != 0) {
        FAIL("the first body is not the plain one");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r2.body_len != sizeof(our_stream_body) - 1 ||
        memcmp(r2.body, our_stream_body, r2.body_len) != 0) {
        FAIL("the second body is not the chunked one");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    /* The old one-turn API still works on the same connection. */
    if (http_client_connection_request(&c, "GET", "/", NULL, 0, &r3) != 0) {
        FAIL("the old request API failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r3.body_len != sizeof(our_body) - 1 || memcmp(r3.body, our_body, r3.body_len) != 0) {
        FAIL("the old request API returned the wrong body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_node_answers_a_pipelined_pair(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;
    http_response r2;

    TEST("Node answers a pipelined pair, in order");
    port = spawn_node_peer(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "GET", "/a", NULL, 0) != 0 ||
        http_client_connection_write_request(&c, "GET", "/b", NULL, 0) != 0) {
        FAIL("a write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r1) != 0) {
        FAIL("the first read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r2) != 0) {
        FAIL("the second read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    /* Node's bodies NAME THEIR PATH, so the order is in the bytes. */
    if (strcmp(r1.body, "body for /a\n") != 0) {
        FAIL("the first body is not Node's answer to /a");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strcmp(r2.body, "body for /b\n") != 0) {
        FAIL("the second body is not Node's answer to /b");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_a_peer_that_answers_once_leaves_the_second_read_failed(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;

    TEST("a peer that answers once leaves the second read failed");
    port = spawn_one_and_close_peer(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "GET", "/a", NULL, 0) != 0) {
        FAIL("the first write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r1) != 0) {
        FAIL("the first read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strcmp(r1.body, "body for /a\n") != 0) {
        FAIL("the first body is wrong");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "GET", "/b", NULL, 0) != 0) {
        FAIL("the second write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    alarm(10);
    if (http_client_connection_read_response(&c, &r1) == 0) {
        alarm(0);
        FAIL("the second read should have failed: the peer is gone");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    alarm(0);
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);
    printf("Stage 65.15b -- the client pipelines\n\n");

    test_the_write_and_the_read_are_separate();
    test_two_requests_go_out_before_either_response();
    test_the_responses_come_back_in_order();
    test_node_answers_a_pipelined_pair();
    test_a_peer_that_answers_once_leaves_the_second_read_failed();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
