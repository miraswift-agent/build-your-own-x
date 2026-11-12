/*
 * Stage 65.18 -- the server reads the request body.
 *
 * The board's claim: "today the server answers a request and leaves its
 * body in the socket, so on a keep-alive connection those bytes are
 * parsed as the NEXT request."
 *
 * Measured, that claim is half true. The request parser already knows
 * Content-Length and already consumes the body bytes -- it moves the
 * bytes that follow the body to the front of its buffer, so the next
 * request starts where the body ended. The framing was never broken.
 *
 * What the server could NOT do is ACT on the body: the parsed body sat
 * in a struct field nobody read, and the answer to a POST was the same
 * document as the answer to a GET.
 *
 * So the claim under test is:
 *
 *   a request with a body is answered with THAT body, and the request
 *   that follows it on the same connection is still answered
 *   correctly.
 *
 * The second half is the framing half: if the body were left in the
 * socket, the second answer would be the echo of the body, not the
 * document. The test writes both requests on one socket and reads two
 * answers.
 *
 * The oracle is our own client (it already has a body parameter) and,
 * independently, curl against the running server.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../src/http_client.h"
#include "../src/sockets.h"

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

/* A hang is a poor failure mode: if the server waits for a body that
 * never arrives, this fires and the test fails with a reason. */
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

/* 1. A request with a body is answered with that body. */
static void test_a_request_with_a_body_is_answered_with_that_body(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char sent[] = "hello, body";

    TEST("a request with a body is answered with that body");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "POST", "/echo", sent,
                                             sizeof(sent) - 1) != 0) {
        FAIL("the write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != sizeof(sent) - 1 || memcmp(r.body, sent, r.body_len) != 0) {
        FAIL("the answer is not the body we sent");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 2. The echo is byte for byte: a body is bytes, not a C string. */
static void test_the_echo_is_byte_for_byte(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char sent[] = { 'a', '\0', (char)0xff, 'b' };

    TEST("the echo is byte for byte");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "POST", "/echo", sent,
                                             sizeof(sent)) != 0) {
        FAIL("the write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != sizeof(sent) || memcmp(r.body, sent, sizeof(sent)) != 0) {
        FAIL("the echo changed the bytes");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 3. A body and then another request on the same connection: the
 *    second answer is the second request's answer. */
static void test_a_body_and_then_another_request(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r1;
    http_response r2;
    static const char sent[] = "hello, body";

    TEST("a body and then another request on the same connection");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "POST", "/echo", sent,
                                             sizeof(sent) - 1) != 0 ||
        http_client_connection_write_request(&c, "GET", "/", NULL, 0) != 0) {
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
    if (r1.body_len != sizeof(sent) - 1 || memcmp(r1.body, sent, r1.body_len) != 0) {
        FAIL("the first answer is not the echo");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r2.body_len != sizeof(our_body) - 1 ||
        memcmp(r2.body, our_body, r2.body_len) != 0) {
        FAIL("the second answer is not the document -- the body was left in the socket");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 4. A body that arrives in pieces: the head first, the body later. */
static void test_a_body_that_arrives_in_pieces(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char buf[4096];
    size_t total = 0;
    ssize_t n;
    static const char head[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "\r\n";
    static const char sent[] = "hello, body";

    TEST("a body that arrives in pieces");
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (sock_write_once(fd, head, sizeof(head) - 1) != (ssize_t)(sizeof(head) - 1)) {
        FAIL("the head did not go out");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    /* Give the server time to read the head alone: it must wait for
     * the body rather than answer a request that is not complete. */
    usleep(100000);
    if (sock_write_once(fd, sent, sizeof(sent) - 1) != (ssize_t)(sizeof(sent) - 1)) {
        FAIL("the body did not go out");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    while ((n = sock_read_once(fd, buf + total, sizeof(buf) - total)) > 0) {
        total += (size_t)n;
    }
    sock_close(fd);
    waitpid(pid, NULL, 0);
    if (total < sizeof(sent) - 1 ||
        memcmp(buf + total - (sizeof(sent) - 1), sent, sizeof(sent) - 1) != 0) {
        FAIL("the echo did not arrive");
        return;
    }
    PASS();
}

/* 5. A request without a body echoes nothing. */
static void test_a_request_without_a_body_echoes_nothing(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;

    TEST("a request without a body echoes nothing");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "GET", "/echo", NULL, 0) != 0) {
        FAIL("the write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != 0) {
        FAIL("the echo of nothing is not nothing");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);
    printf("Stage 65.18 -- the server reads the request body\n\n");

    alarm(30);
    test_a_request_with_a_body_is_answered_with_that_body();
    test_the_echo_is_byte_for_byte();
    test_a_body_and_then_another_request();
    test_a_body_that_arrives_in_pieces();
    test_a_request_without_a_body_echoes_nothing();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
