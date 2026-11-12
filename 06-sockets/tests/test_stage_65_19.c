/*
 * Stage 65.19 -- the server reads a chunked request body.
 *
 * Stage 65.18 shipped /echo: the server answers a request with the body
 * it read. But it could only read a body that came with a
 * Content-Length. The other framing -- the one the header itself
 * announces -- was still a hole, and Stage 65.18's close-out measured
 * it:
 *
 *   a chunked request body is not read at all. The server answers it
 *   with Content-Length: 0 and leaves the chunk bytes in the socket,
 *   where they are parsed as the NEXT request.
 *
 * So this stage gives the server the other framing. The decoder from
 * Stage 65.11 is already written and already tested; what the server
 * needs is the mirror of what the CLIENT did in Stage 65.13.
 *
 * The claim under test:
 *
 *   a request whose body is chunked is answered with the DECODED body,
 *   and the bytes after the terminal chunk are the start of the next
 *   message -- not part of this one.
 *
 * The oracle is a capture, not a memory: the 135 bytes Node's http
 * request actually sent for a chunked upload (two chunks, then the
 * terminal chunk), kept in tests/node_capture.h and replayed here. The
 * live check is curl, which is not us.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

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

/* A hang is a poor failure mode: if the server waits for a chunk that
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

/* Write raw bytes on the client's socket: the request is captured
 * wire traffic, not something our own builder would have written. */
static int write_raw(http_client_connection *c, const void *bytes, size_t len)
{
    return sock_write_once(c->fd, bytes, len) == (ssize_t)len ? 0 : -1;
}

/* 1. A chunked request body is decoded and echoed. */
static void test_a_chunked_request_body_is_decoded_and_echoed(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char decoded[] = "Hello, chunked world!\n";

    TEST("a chunked request body is decoded and echoed");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, node_chunked_request, NODE_CHUNKED_REQUEST_LEN) != 0) {
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
    if (r.body_len != sizeof(decoded) - 1 ||
        memcmp(r.body, decoded, r.body_len) != 0) {
        FAIL("the answer is not the decoded body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 2. A chunked body is byte for byte: a body is bytes, not a C string. */
static void test_a_chunked_body_is_byte_for_byte(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char payload[] = { 'a', '\0', (char)0xff, 'b' };
    char req[512];
    int n;

    TEST("a chunked body is byte for byte");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    n = snprintf(req, sizeof(req),
                 "POST /echo HTTP/1.1\r\n"
                 "Host: test\r\n"
                 "Transfer-Encoding: chunked\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%zx\r\n", sizeof(payload));
    if (n < 0 || (size_t)n + sizeof(payload) + 8 > sizeof(req)) {
        FAIL("the request did not fit");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    memcpy(req + n, payload, sizeof(payload));
    n += (int)sizeof(payload);
    n += snprintf(req + n, sizeof(req) - (size_t)n, "\r\n0\r\n\r\n");
    if (write_raw(&c, req, (size_t)n) != 0) {
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
    if (r.body_len != sizeof(payload) || memcmp(r.body, payload, sizeof(payload)) != 0) {
        FAIL("the echo changed the bytes");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 3. A chunked body and then another request on the same connection:
 *    the bytes after the terminal chunk are the NEXT message. */
static void test_a_chunked_body_and_then_another_request(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r1;
    http_response r2;
    static const char decoded[] = "Hello, chunked world!\n";
    static const char second[] =
        "GET / HTTP/1.1\r\n"
        "Host: test\r\n"
        "\r\n";

    TEST("a chunked body and then another request on the same connection");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, node_chunked_request, NODE_CHUNKED_REQUEST_LEN) != 0 ||
        write_raw(&c, second, sizeof(second) - 1) != 0) {
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
    if (r1.body_len != sizeof(decoded) - 1 ||
        memcmp(r1.body, decoded, r1.body_len) != 0) {
        FAIL("the first answer is not the decoded body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r2.body_len != sizeof(our_body) - 1 ||
        memcmp(r2.body, our_body, r2.body_len) != 0) {
        FAIL("the second answer is not the document -- the chunk bytes were left in the socket");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 4. A chunked body that arrives in pieces: the head first, the chunks
 *    later. A request that is not complete is not an error. */
static void test_a_chunked_body_that_arrives_in_pieces(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    size_t head_len = 0;
    size_t i;
    static const char decoded[] = "Hello, chunked world!\n";

    TEST("a chunked body that arrives in pieces");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    /* The capture is a byte array, not a C string: find the end of the
     * headers by looking at the bytes. */
    for (i = 0; i + 3 < NODE_CHUNKED_REQUEST_LEN; i++) {
        if (node_chunked_request[i] == '\r' &&
            node_chunked_request[i + 1] == '\n' &&
            node_chunked_request[i + 2] == '\r' &&
            node_chunked_request[i + 3] == '\n') {
            head_len = i + 4;
            break;
        }
    }
    if (head_len == 0) {
        FAIL("the capture has no header end");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, node_chunked_request, head_len) != 0) {
        FAIL("the head did not go out");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    /* Give the server time to read the head alone: it must wait for
     * the chunks rather than answer a request that is not complete. */
    usleep(100000);
    if (write_raw(&c, node_chunked_request + head_len,
                  NODE_CHUNKED_REQUEST_LEN - head_len) != 0) {
        FAIL("the chunks did not go out");
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
    if (r.body_len != sizeof(decoded) - 1 ||
        memcmp(r.body, decoded, r.body_len) != 0) {
        FAIL("the decoded body is not the decoded body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 5. An empty chunked body echoes nothing: the terminal chunk on its
 *    own is a complete body. */
static void test_an_empty_chunked_body_echoes_nothing(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "0\r\n\r\n";

    TEST("an empty chunked body echoes nothing");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, req, sizeof(req) - 1) != 0) {
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
    printf("Stage 65.19 -- the server reads a chunked request body\n\n");

    alarm(30);
    test_a_chunked_request_body_is_decoded_and_echoed();
    test_a_chunked_body_is_byte_for_byte();
    test_a_chunked_body_and_then_another_request();
    test_a_chunked_body_that_arrives_in_pieces();
    test_an_empty_chunked_body_echoes_nothing();
    /* Stage 65.21 replaced this stage's sixth test. "A coding overrides
     * a length" was the SAFE half of RFC 9112 6.3 item 3, and it was
     * this SERVER's behavior; Stage 65.21 gives the server the loud
     * half instead -- the request is refused with 400 -- so the claim
     * moved to tests/test_stage_65_21.c rather than dying here. */

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
