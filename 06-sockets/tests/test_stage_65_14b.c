/*
 * Stage 65.14b -- the connection object: a second request on the same socket.
 *
 * Stage 65.14a made the parser able to hold the bytes that follow a
 * response. This stage gives the client something to hold them in: a
 * connection that owns a socket, writes a request, reads the response,
 * and can write ANOTHER request on the same socket.
 *
 * The claim under test is on the wire, not in our opinion of it:
 *
 *   a second request on the same socket must get a second response,
 *   and the server must not have closed in between.
 *
 * The peer says so itself. Our server answers a keep-alive request with
 * "Connection: keep-alive" and keeps the socket open; if the client
 * asked for a close (as it did until this stage), the server would hang
 * up after the first response and the second request could not be
 * answered at all. So the test discriminates: a client that sends
 * "Connection: close" cannot pass it.
 *
 * The independent oracle is Node again: the same captured bytes, replayed
 * twice on one socket. The client must return Node's body twice.
 */

#include "../src/http_client.h"
#include "../src/sockets.h"
#include "node_capture.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

static const char our_body[] = "hello from our server\n";

/* Our own server: it supports keep-alive since Stage 65.6. */
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

/* Read the client's requests until `count` of them have arrived.
 *
 * A child that closes while the client's request is still unread makes
 * the kernel send an RST -- and an RST discards the data the peer sent
 * us. That is not a detail: it is what made this test fail in the full
 * suite and pass alone. A peer that wants to hang up cleanly drains
 * first. */
static void drain_requests(int fd, int count)
{
    char buf[4096];
    size_t len = 0;
    size_t scan = 0;
    int seen = 0;

    while (seen < count && len + 4 < sizeof(buf)) {
        ssize_t n = read(fd, buf + len, sizeof(buf) - len);
        if (n <= 0) {
            return;
        }
        len += (size_t)n;
        while (scan + 4 <= len) {
            if (memcmp(buf + scan, "GET ", 4) == 0) {
                seen++;
            }
            scan++;
        }
        /* Keep three bytes of overlap, so a request line split across
         * two reads is still counted once. */
        scan = len >= 3 ? len - 3 : 0;
    }
}

/* Node's bytes, replayed `copies` times on one socket. */
static uint16_t spawn_node_server(int copies, pid_t *pid_out)
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
            int i;
            for (i = 0; i < copies; i++) {
                size_t off = 0;
                while (off < NODE_RESPONSE_LEN) {
                    ssize_t n = write(fd, node_response + off, NODE_RESPONSE_LEN - off);
                    if (n <= 0) {
                        break;
                    }
                    off += (size_t)n;
                }
            }
            /* Drain before hanging up: a close with an unread request
             * on the socket is an RST, and an RST throws away the bytes
             * we just wrote. */
            drain_requests(fd, copies);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

static void test_the_request_states_the_connections_fate(void)
{
    char req[1024];
    int n;

    TEST("the request states the connection's fate");
    n = http_client_request_build_ex(req, sizeof(req), "GET", "/", "example.com",
                                      NULL, 0, 1);
    if (n < 0) {
        FAIL("the keep-alive request did not build");
        return;
    }
    if (strstr(req, "Connection: keep-alive\r\n") == NULL) {
        FAIL("the keep-alive request does not say so");
        return;
    }
    /* The old one-request API keeps its old behavior: it asks to close. */
    n = http_client_request_build(req, sizeof(req), "GET", "/", "example.com", NULL, 0);
    if (n < 0) {
        FAIL("the old request did not build");
        return;
    }
    if (strstr(req, "Connection: close\r\n") == NULL) {
        FAIL("the old request no longer asks to close");
        return;
    }
    PASS();
}

static void test_two_requests_on_one_socket_get_two_responses(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;
    http_response r2;
    int fd_before;

    TEST("two requests on one socket get two responses");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    fd_before = c.fd;
    if (http_client_connection_request(&c, "GET", "/", NULL, 0, &r1) != 0) {
        FAIL("the first request failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_request(&c, "GET", "/", NULL, 0, &r2) != 0) {
        FAIL("the second request failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (c.fd != fd_before) {
        FAIL("the second request used a different socket");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r1.body_len != sizeof(our_body) - 1 || memcmp(r1.body, our_body, r1.body_len) != 0) {
        FAIL("the first body is wrong");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r2.body_len != sizeof(our_body) - 1 || memcmp(r2.body, our_body, r2.body_len) != 0) {
        FAIL("the second body is wrong");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_server_did_not_close_between_the_requests(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;
    const char *conn;

    TEST("the server did not close between the requests");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_request(&c, "GET", "/", NULL, 0, &r1) != 0) {
        FAIL("the request failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    conn = http_response_header_get(&r1, "Connection");
    if (conn == NULL) {
        FAIL("the response does not say what happens to the connection");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strcasecmp(conn, "close") == 0) {
        FAIL("the server said it was closing");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_a_peer_that_closes_leaves_the_next_request_failed(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;

    TEST("a peer that closes leaves the next request failed");
    port = spawn_node_server(1, &pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_request(&c, "GET", "/", NULL, 0, &r1) != 0) {
        FAIL("the first request failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_request(&c, "GET", "/", NULL, 0, &r1) == 0) {
        FAIL("the second request should have failed: the peer is gone");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_two_of_nodes_responses_come_back_on_one_socket(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r1;
    http_response r2;
    static const char expected[] = "Hello, chunked world!\n";

    TEST("two of Node's responses come back on one socket");
    port = spawn_node_server(2, &pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_request(&c, "GET", "/", NULL, 0, &r1) != 0) {
        FAIL("the first request failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_request(&c, "GET", "/", NULL, 0, &r2) != 0) {
        FAIL("the second request failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r1.body_len != sizeof(expected) - 1 || memcmp(r1.body, expected, r1.body_len) != 0) {
        FAIL("the first body is not Node's");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r2.body_len != sizeof(expected) - 1 || memcmp(r2.body, expected, r2.body_len) != 0) {
        FAIL("the second body is not Node's");
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
    printf("Stage 65.14b -- the connection object: a second request on the same socket\n\n");

    test_the_request_states_the_connections_fate();
    test_two_requests_on_one_socket_get_two_responses();
    test_the_server_did_not_close_between_the_requests();
    test_a_peer_that_closes_leaves_the_next_request_failed();
    test_two_of_nodes_responses_come_back_on_one_socket();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
