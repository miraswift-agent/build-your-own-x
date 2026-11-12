/*
 * Stage 65.15a -- the measurement: does the server's parser keep the bytes
 * that follow a request it has already answered?
 *
 * The board predicted a gap here, by symmetry with Stage 65.14a: the
 * client's parser dropped the bytes of the next response that arrived in
 * the same read, and nobody saw it, because the client only ever made
 * one request per connection. The server has the same shape of blind
 * spot -- with the next REQUEST instead of the next response.
 *
 * So this stage measures before it writes. The answer, at the parser
 * level and at the socket level:
 *
 *   the server's parser already keeps the leftovers, and the serve loop
 *   already asks the parser before it reads the socket.
 *
 * The gap is not on the server side at all. It is on the client side:
 * the client writes a request and waits for its response before it
 * writes the next one. That is Stage 65.15b.
 *
 * These tests pin what the measurement found, so the capability cannot
 * regress silently while the client is taught to use it.
 */

#include "../src/http.h"
#include "../src/http_client.h"
#include "../src/sockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

/* Read until the needle is in the buffer.
 *
 * A keep-alive server does not close the connection, so "read until
 * EOF" would wait forever: the conversation is still open. What ends
 * the read is the second response arriving. */
static size_t read_until(int fd, char *buf, size_t cap, const char *needle)
{
    size_t seen = 0;
    while (seen + 1 < cap) {
        ssize_t got = read(fd, buf + seen, cap - seen - 1);
        if (got <= 0) {
            break;
        }
        seen += (size_t)got;
        buf[seen] = '\0';
        if (strstr(buf, needle) != NULL) {
            break;
        }
    }
    buf[seen] = '\0';
    return seen;
}

static const char req_a[] =
    "GET / HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "Connection: keep-alive\r\n"
    "\r\n";

static const char req_b[] =
    "GET /nope HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "Connection: keep-alive\r\n"
    "\r\n";

/* ---- the measurement, at the parser level ---- */

static void test_the_parser_keeps_the_bytes_that_follow_a_request(void)
{
    http_parser p;
    http_request r1;
    http_request r2;
    char buf[4096];
    size_t n;

    TEST("the parser keeps the bytes that follow a request");
    http_parser_init(&p);
    /* Two requests in one feed -- exactly what a socket read can deliver. */
    n = (size_t)snprintf(buf, sizeof(buf), "%s%s", req_a, req_b);
    if (n >= sizeof(buf)) {
        FAIL("the fixture does not fit");
        return;
    }
    if (http_parser_feed(&p, buf, n) != 0) {
        FAIL("the feed failed");
        return;
    }
    if (http_parser_next(&p, &r1) != HTTP_PARSE_OK) {
        FAIL("the first request did not parse");
        return;
    }
    if (strcmp(r1.target, "/") != 0) {
        FAIL("the first request is the wrong one");
        return;
    }
    /* No second feed. The second request must already be there. */
    if (http_parser_next(&p, &r2) != HTTP_PARSE_OK) {
        FAIL("the second request was not kept: the parser dropped it");
        return;
    }
    if (strcmp(r2.target, "/nope") != 0) {
        FAIL("the second request is the wrong one");
        return;
    }
    PASS();
}

static void test_the_parser_keeps_a_body_and_the_bytes_after_it(void)
{
    http_parser p;
    http_request r1;
    http_request r2;
    char buf[4096];
    size_t n;

    TEST("the parser keeps a body, and the bytes after it");
    http_parser_init(&p);
    n = (size_t)snprintf(buf, sizeof(buf),
                         "POST / HTTP/1.1\r\n"
                         "Content-Length: 5\r\n"
                         "\r\n"
                         "hello"
                         "%s", req_b);
    if (n >= sizeof(buf)) {
        FAIL("the fixture does not fit");
        return;
    }
    if (http_parser_feed(&p, buf, n) != 0) {
        FAIL("the feed failed");
        return;
    }
    if (http_parser_next(&p, &r1) != HTTP_PARSE_OK) {
        FAIL("the first request did not parse");
        return;
    }
    if (r1.body_len != 5 || memcmp(r1.body, "hello", 5) != 0) {
        FAIL("the body is wrong");
        return;
    }
    if (http_parser_next(&p, &r2) != HTTP_PARSE_OK) {
        FAIL("the request after the body was not kept");
        return;
    }
    if (strcmp(r2.target, "/nope") != 0) {
        FAIL("the request after the body is the wrong one");
        return;
    }
    PASS();
}

/* ---- the measurement, at the socket level ---- */

static void test_two_requests_in_one_write_get_two_responses(void)
{
    sock_listener l;
    pid_t pid;
    char buf[4096];
    size_t n;

    TEST("two requests in one write get two responses");
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
            http_serve_connection(fd, "hello\n", &st);
            sock_close(fd);
        }
        _exit(0);
    }
    {
        int fd = sock_connect_loopback(l.port);
        if (fd < 0) {
            perror("sock_connect");
            exit(1);
        }
        n = (size_t)snprintf(buf, sizeof(buf), "%s%s", req_a, req_b);
        if (write(fd, buf, n) != (ssize_t)n) {
            perror("write");
            exit(1);
        }
        read_until(fd, buf, sizeof(buf), "HTTP/1.1 404 Not Found");
        sock_close(fd);
    }
    waitpid(pid, NULL, 0);
    /* Two responses: "HTTP/1.1 200 OK" and "HTTP/1.1 404 Not Found". */
    if (strstr(buf, "HTTP/1.1 200 OK") == NULL) {
        FAIL("the first response is missing");
        return;
    }
    if (strstr(buf, "HTTP/1.1 404 Not Found") == NULL) {
        FAIL("the second response is missing");
        return;
    }
    {
        char *first = strstr(buf, "HTTP/1.1 200 OK");
        char *second = strstr(buf, "HTTP/1.1 404 Not Found");
        if (first && second && second < first) {
            FAIL("the responses came back out of order");
            return;
        }
    }
    PASS();
}

static void test_the_server_did_not_close_between_the_two_responses(void)
{
    sock_listener l;
    pid_t pid;
    char buf[4096];
    size_t n;

    TEST("the server did not close between the two responses");
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
            http_serve_connection(fd, "hello\n", &st);
            sock_close(fd);
        }
        _exit(0);
    }
    {
        int fd = sock_connect_loopback(l.port);
        if (fd < 0) {
            perror("sock_connect");
            exit(1);
        }
        n = (size_t)snprintf(buf, sizeof(buf), "%s%s", req_a, req_b);
        if (write(fd, buf, n) != (ssize_t)n) {
            perror("write");
            exit(1);
        }
        read_until(fd, buf, sizeof(buf), "HTTP/1.1 404 Not Found");
        sock_close(fd);
    }
    waitpid(pid, NULL, 0);
    if (strstr(buf, "Connection: keep-alive") == NULL) {
        FAIL("the server did not say it was keeping the connection");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.15a -- the measurement: the server's parser and the bytes that follow\n\n");

    test_the_parser_keeps_the_bytes_that_follow_a_request();
    test_the_parser_keeps_a_body_and_the_bytes_after_it();
    test_two_requests_in_one_write_get_two_responses();
    test_the_server_did_not_close_between_the_two_responses();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
