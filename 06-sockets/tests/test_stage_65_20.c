/*
 * Stage 65.20 -- the client sends a chunked request body.
 *
 * The framing matrix is nearly complete:
 *
 *   server sends chunked      65.12
 *   client receives chunked   65.13
 *   server receives chunked   65.19
 *   client sends chunked      this stage -- the last corner
 *
 * The claim under test:
 *
 *   the client can put a request body on the wire in pieces, saying
 *   "Transfer-Encoding: chunked" and naming no length -- because the
 *   length is not known until the body is done.
 *
 * The shape of the API is the lesson. The head goes out first, the body
 * follows as chunks, and the terminal chunk says the body has ended. A
 * body whose length is already in hand should carry Content-Length
 * instead: chunked is for a length that is not known yet, not for a
 * body that happens to be large.
 *
 * The oracle is Node's own bytes, captured off the wire in Stage 65.19
 * and kept in tests/node_capture.h: 135 bytes for a chunked upload,
 * two chunks and the terminal chunk. This stage writes the same two
 * chunks and compares the framing byte for byte -- two implementations
 * agreeing on bytes, not on prose. The live check is Node's HTTP
 * server, which decodes our upload into one body.
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

/* A hang is a poor failure mode: if the peer waits for a chunk that
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

/* Our own server: one connection, one document, and /echo answers with
 * the body it read (Stage 65.18 and Stage 65.19). */
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

/* Has the terminal chunk arrived?
 *
 * The terminal chunk is "0\r\n\r\n" and the chunk before it ends with
 * its own CRLF, so a request whose body is finished ends with
 * "\r\n0\r\n\r\n". */
static int has_terminal_chunk(const unsigned char *buf, size_t len)
{
    static const char term[] = "\r\n0\r\n\r\n";
    const size_t tlen = sizeof(term) - 1;
    size_t i;

    if (len < tlen) {
        return 0;
    }
    for (i = len - tlen;; i--) {
        if (memcmp(buf + i, term, tlen) == 0) {
            return 1;
        }
        if (i == 0) {
            break;
        }
    }
    return 0;
}

/* A peer that captures the bytes the client wrote.
 *
 * The capture comes back through a pipe rather than over the socket,
 * because what is being tested is what went OUT -- not what the peer
 * thought to answer. */
static uint16_t spawn_capture_peer(pid_t *pid_out, int *capture_fd)
{
    sock_listener l;
    pid_t pid;
    int fds[2];

    if (pipe(fds) != 0) {
        perror("pipe");
        exit(1);
    }
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
        int fd;
        unsigned char buf[4096];
        size_t total = 0;

        close(fds[0]);
        fd = sock_accept(&l);
        if (fd >= 0) {
            for (;;) {
                ssize_t n = read(fd, buf + total, sizeof(buf) - total);
                if (n <= 0) {
                    break;
                }
                total += (size_t)n;
                if (has_terminal_chunk(buf, total) || total == sizeof(buf)) {
                    break;
                }
            }
            {
                size_t written = 0;
                while (written < total) {
                    ssize_t n = write(fds[1], buf + written, total - written);
                    if (n <= 0) {
                        break;
                    }
                    written += (size_t)n;
                }
            }
            {
                static const char resp[] =
                    "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
                ssize_t n = write(fd, resp, sizeof(resp) - 1);
                (void)n;
            }
            sock_close(fd);
        }
        close(fds[1]);
        _exit(0);
    }
    close(fds[1]);
    *capture_fd = fds[0];
    *pid_out = pid;
    return l.port;
}

/* The offset just past the end of the headers, or 0 if there is no
 * header end. The capture is a byte array, not a C string: the bytes
 * are looked at, not searched for a terminator. */
static size_t head_end(const unsigned char *buf, size_t len)
{
    size_t i;

    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i + 4;
        }
    }
    return 0;
}

/* 1. The head says chunked, and names no length. */
static void test_the_head_says_chunked_and_says_no_length(void)
{
    char head[512];
    int n;
    static const char want_line[] = "POST /echo HTTP/1.1\r\n";

    TEST("the head says chunked and says no length");
    n = http_client_request_build_chunked_head(head, sizeof(head), "POST", "/echo",
                                                "example.test", 1);
    if (n < 0) {
        FAIL("the head did not build");
        return;
    }
    if (strncmp(head, want_line, sizeof(want_line) - 1) != 0) {
        FAIL("the request line is wrong");
        return;
    }
    if (strstr(head, "Transfer-Encoding: chunked\r\n") == NULL) {
        FAIL("the head does not say chunked");
        return;
    }
    if (strstr(head, "Content-Length") != NULL) {
        FAIL("the head names a length it does not have");
        return;
    }
    PASS();
}

/* 2. The bytes on the wire are the bytes Node wrote. */
static void test_the_bytes_on_the_wire_are_the_bytes_node_wrote(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    int capture_fd;
    unsigned char captured[4096];
    size_t captured_len = 0;
    size_t ours;
    size_t node;

    TEST("the bytes on the wire are the bytes Node wrote");
    port = spawn_capture_peer(&pid, &capture_fd);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        close(capture_fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_chunked_head(&c, "POST", "/echo") != 0 ||
        http_client_connection_write_chunk(&c, "Hello, ", 7) != 0 ||
        http_client_connection_write_chunk(&c, "chunked world!\n", 15) != 0 ||
        http_client_connection_write_chunked_end(&c) != 0) {
        FAIL("the request did not go out");
        http_client_connection_close(&c);
        close(capture_fd);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    for (;;) {
        ssize_t n = read(capture_fd, captured + captured_len,
                         sizeof(captured) - captured_len);
        if (n <= 0) {
            break;
        }
        captured_len += (size_t)n;
        if (captured_len == sizeof(captured)) {
            break;
        }
    }
    close(capture_fd);
    waitpid(pid, NULL, 0);

    ours = head_end(captured, captured_len);
    node = head_end(node_chunked_request, NODE_CHUNKED_REQUEST_LEN);
    if (ours == 0 || node == 0) {
        FAIL("a capture has no header end");
        return;
    }
    if (captured_len - ours != NODE_CHUNKED_REQUEST_LEN - node) {
        FAIL("the framing is a different length than Node's");
        return;
    }
    if (memcmp(captured + ours, node_chunked_request + node,
               NODE_CHUNKED_REQUEST_LEN - node) != 0) {
        FAIL("the framing is not the framing Node wrote");
        return;
    }
    PASS();
}

/* 3. A chunked request body is read by the server, as one body. */
static void test_a_chunked_request_body_is_read_by_the_server(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char decoded[] = "Hello, chunked world!\n";

    TEST("a chunked request body is read by the server");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_chunked_head(&c, "POST", "/echo") != 0 ||
        http_client_connection_write_chunk(&c, "Hello, ", 7) != 0 ||
        http_client_connection_write_chunk(&c, "chunked world!\n", 15) != 0 ||
        http_client_connection_write_chunked_end(&c) != 0) {
        FAIL("the request did not go out");
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
        FAIL("the answer is not the body we sent");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 4. A chunked body is byte for byte: a body is bytes, not a string. */
static void test_a_chunked_body_is_byte_for_byte(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char payload[] = { 'a', '\0', (char)0xff, 'b' };

    TEST("a chunked body is byte for byte");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_chunked_head(&c, "POST", "/echo") != 0 ||
        http_client_connection_write_chunk(&c, payload, sizeof(payload)) != 0 ||
        http_client_connection_write_chunked_end(&c) != 0) {
        FAIL("the request did not go out");
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
    if (r.body_len != sizeof(payload) || memcmp(r.body, payload, r.body_len) != 0) {
        FAIL("the echo changed the bytes");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 5. An empty chunked body is the head and the terminal chunk. */
static void test_an_empty_chunked_body_is_the_terminal_chunk_alone(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;

    TEST("an empty chunked body is the terminal chunk alone");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_chunked_head(&c, "POST", "/echo") != 0 ||
        http_client_connection_write_chunked_end(&c) != 0) {
        FAIL("the request did not go out");
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

/* 6. The connection refuses the shapes that would put two messages on
 *    the wire at once: a chunk before the head, a zero-length chunk
 *    (which IS the terminal chunk), and a normal request in the middle
 *    of a chunked body. */
static void test_the_connection_refuses_mixed_framing(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;

    TEST("the connection refuses mixed framing");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    errno = 0;
    if (http_client_connection_write_chunk(&c, "x", 1) == 0) {
        FAIL("a chunk before the head was allowed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno != EINVAL) {
        FAIL("a chunk before the head did not fail with EINVAL");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_chunked_head(&c, "POST", "/echo") != 0) {
        FAIL("the head did not go out");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    errno = 0;
    if (http_client_connection_write_chunk(&c, "", 0) == 0) {
        FAIL("a zero-length chunk was allowed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno != EINVAL) {
        FAIL("a zero-length chunk did not fail with EINVAL");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    errno = 0;
    if (http_client_connection_write_request(&c, "GET", "/", NULL, 0) == 0) {
        FAIL("a request inside a chunked body was allowed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno != EINVAL) {
        FAIL("a request inside a chunked body did not fail with EINVAL");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    /* The body still finishes normally. */
    if (http_client_connection_write_chunk(&c, "ok", 2) != 0 ||
        http_client_connection_write_chunked_end(&c) != 0) {
        FAIL("the body did not go out");
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
    if (r.body_len != 2 || memcmp(r.body, "ok", 2) != 0) {
        FAIL("the answer is not the body we sent");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 7. A chunked request, and then another request on one connection:
 *    the terminal chunk ends the MESSAGE, not the conversation. */
static void test_a_chunked_request_and_then_another_request(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r1;
    http_response r2;
    static const char decoded[] = "Hello, chunked world!\n";

    TEST("a chunked request and then another request");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_chunked_head(&c, "POST", "/echo") != 0 ||
        http_client_connection_write_chunk(&c, "Hello, ", 7) != 0 ||
        http_client_connection_write_chunk(&c, "chunked world!\n", 15) != 0 ||
        http_client_connection_write_chunked_end(&c) != 0) {
        FAIL("the request did not go out");
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
    if (http_client_connection_write_request(&c, "GET", "/", NULL, 0) != 0) {
        FAIL("the second request did not go out");
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
        FAIL("the first answer is not the body we sent");
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

/* 8. A body written in pieces is one body: the head, a piece, a pause,
 *    another piece, and the terminal chunk. This is the shape chunked
 *    exists for -- the body is not all in hand when the head goes out. */
static void test_a_body_written_in_pieces_is_one_body(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char decoded[] = "Hello, chunked world!\n";

    TEST("a body written in pieces is one body");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_chunked_head(&c, "POST", "/echo") != 0 ||
        http_client_connection_write_chunk(&c, "Hello, ", 7) != 0) {
        FAIL("the head did not go out");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    /* The server has a head and half a body. It must wait. */
    usleep(100000);
    if (http_client_connection_write_chunk(&c, "chunked world!\n", 15) != 0 ||
        http_client_connection_write_chunked_end(&c) != 0) {
        FAIL("the rest of the body did not go out");
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
        FAIL("the pieces did not arrive as one body");
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
    signal(SIGALRM, on_alarm);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Stage 65.20 -- the client sends a chunked request body\n");

    alarm(30);
    test_the_head_says_chunked_and_says_no_length();
    alarm(30);
    test_the_bytes_on_the_wire_are_the_bytes_node_wrote();
    alarm(30);
    test_a_chunked_request_body_is_read_by_the_server();
    alarm(30);
    test_a_chunked_body_is_byte_for_byte();
    alarm(30);
    test_an_empty_chunked_body_is_the_terminal_chunk_alone();
    alarm(30);
    test_the_connection_refuses_mixed_framing();
    alarm(30);
    test_a_chunked_request_and_then_another_request();
    alarm(30);
    test_a_body_written_in_pieces_is_one_body();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
