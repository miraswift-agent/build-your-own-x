/*
 * Stage 65.31 -- the status decides: 1xx, 204 and 304 have no body.
 *
 * The rule (RFC 9112 6.3 item 1 -- the FIRST item of the precedence
 * list):
 *
 *   "Any response to a HEAD request and any response with a 1xx
 *   (Informational), 204 (No Content), or 304 (Not Modified) status code
 *   is always terminated by the first empty line after the header fields,
 *   regardless of the header fields present in the message, and thus
 *   cannot contain a message body or trailer section."
 *
 * Stage 65.28 handled the HEAD half of that sentence. The other three
 * are different in one way that matters: the STATUS alone decides. The
 * HEAD case needs the conversation -- the connection knows it asked for
 * HEAD, the parser cannot know -- while 1xx, 204 and 304 carry the
 * answer in the status line itself.
 *
 * So the knowledge moves: for HEAD it lives with the connection (Stage
 * 65.28's queue), and for these three it lives in the parser. Two
 * places, two reasons -- the rule does not care where the knowledge
 * comes from, only that it is there.
 *
 * Three consequences, and each one was a bug before this stage:
 *
 *   - A 204 or 304 that carries "Content-Length: 5" has NO body. The
 *     length is ignored, "regardless of the header fields present in
 *     the message". A client that waits for five octets waits forever.
 *
 *   - A 1xx is INTERIM: it is a complete message, but it is not the
 *     answer to the request. The final response follows it. RFC 9112
 *     15.2: "A client MUST be able to parse one or more 1xx responses
 *     received prior to a final response, even if the client does not
 *     expect one."
 *
 *   - Item 1 comes BEFORE item 5. A 204 with "Content-Length: abc" is
 *     not an invalid framing: there is no body for the length to be
 *     invalid about. Stage 65.28 established the precedence for HEAD;
 *     this stage extends it to the three status codes.
 *
 * The server half is a separate story and is NOT in this stage: a
 * server that sends a 1xx or 204 MUST NOT send a Content-Length
 * (RFC 9110 8.6), and our builder always sends one -- so the builder
 * needs a no-content mode and a route that would use it (a 304 for a
 * conditional GET, a 100 Continue). That is the next stage, named.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

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

/* A hang is a poor failure mode: if a peer waits for something that
 * never comes, this fires and the test fails with a reason. */
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

/* ---------- helpers ---------- */

/* A peer that sends a canned response and then HOLDS the connection
 * open for hold_ms. A client that waits for a body that will never come
 * sits here until the peer hangs up -- which is how a test can tell
 * "the client returned" from "the client waited". */
static uint16_t spawn_holding_peer_ms(pid_t *pid_out, const char *response, int hold_ms)
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
            char buf[1024];
            ssize_t n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, response, strlen(response));
            usleep((useconds_t)hold_ms * 1000);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

/* A peer that sends two responses in one write: an interim and then a
 * final. The client must read past the interim and answer with the
 * final one. */
static uint16_t spawn_two_response_peer(pid_t *pid_out, const char *first,
                                        const char *second, int hold_ms)
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
            char buf[1024];
            ssize_t n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, first, strlen(first));
            (void)write(fd, second, strlen(second));
            usleep((useconds_t)hold_ms * 1000);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

/* A peer that answers TWO requests on one connection: the first with
 * the canned response, the second with a plain 200. */
static uint16_t spawn_two_turn_peer(pid_t *pid_out, const char *first)
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
            char buf[1024];
            ssize_t n;
            /* A deadline of its own: a client that waits for a body
             * that will never come must not take the whole suite down
             * with it. The peer gives up and closes; the client sees
             * the close and the test fails with a reason. */
            struct timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, first, strlen(first));
            n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi",
                        sizeof("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi") - 1);
            usleep(200000);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

/* Open a connection and write one request on it. */
static int connect_and_write(uint16_t port, const char *request)
{
    int fd = sock_connect_loopback(port);
    size_t len = strlen(request);
    size_t sent = 0;

    if (fd < 0) {
        return -1;
    }
    while (sent < len) {
        ssize_t n = write(fd, request + sent, len - sent);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            sock_close(fd);
            return -1;
        }
        sent += (size_t)n;
    }
    return fd;
}

static const char req_get[] =
    "GET / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n";
static const char req_head[] =
    "HEAD / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n";

/* ---------- 1. a 204 with a length has no body ---------- */

static void test_204_with_a_length_has_no_body(void)
{
    static const char resp[] =
        "HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("a 204 with a Content-Length has no body");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, resp, 3000);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not return the 204");
    } else if (r.status != 204) {
        FAIL("the status was not 204");
    } else if (r.body_len != 0) {
        FAIL("the 204 had a body");
    } else {
        PASS();
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 2. a 304 with a length has no body ---------- */

static void test_304_with_a_length_has_no_body(void)
{
    static const char resp[] =
        "HTTP/1.1 304 Not Modified\r\nContent-Length: 5\r\n\r\n";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("a 304 with a Content-Length has no body");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, resp, 3000);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not return the 304");
    } else if (r.status != 304) {
        FAIL("the status was not 304");
    } else if (r.body_len != 0) {
        FAIL("the 304 had a body");
    } else {
        PASS();
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 3. a 1xx is interim: the final response answers ---------- */

static void test_1xx_is_interim(void)
{
    static const char first[] = "HTTP/1.1 100 Continue\r\n\r\n";
    static const char second[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("a 1xx is interim -- the final response is the answer");
    alarm(10);
    port = spawn_two_response_peer(&pid, first, second, 3000);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not return the final response");
    } else if (r.status != 200) {
        FAIL("the client answered with the interim response");
    } else if (r.body_len != 5) {
        FAIL("the final response lost its body");
    } else {
        PASS();
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 4. the interim does not steal the HEAD flag ---------- */

static void test_interim_keeps_the_head_flag(void)
{
    static const char first[] = "HTTP/1.1 100 Continue\r\n\r\n";
    static const char second[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("an interim response does not consume the HEAD request");
    alarm(10);
    port = spawn_two_response_peer(&pid, first, second, 3000);
    fd = connect_and_write(port, req_head);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    c.in_flight[0] = 1;              /* the request was a HEAD */
    c.n_inflight = 1;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not return the final response");
    } else if (r.status != 200) {
        FAIL("the client answered with the interim response");
    } else if (r.body_len != 0) {
        FAIL("the HEAD response waited for a body");
    } else {
        PASS();
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 5. item 1 before item 5 ---------- */

static void test_204_with_an_invalid_length_is_complete(void)
{
    static const char resp[] =
        "HTTP/1.1 204 No Content\r\nContent-Length: abc\r\n\r\n";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("a 204 with an unusable length is still complete");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, resp, 3000);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client refused the 204");
    } else if (r.status != 204) {
        FAIL("the status was not 204");
    } else if (r.content_length != -1) {
        FAIL("the unusable length was reported as a number");
    } else {
        PASS();
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 6. a 204 that names an unknown coding is bodyless ---------- */

static void test_204_with_an_unknown_coding_has_no_body(void)
{
    static const char resp[] =
        "HTTP/1.1 204 No Content\r\nTransfer-Encoding: gzip\r\n\r\n";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("a 204 naming a coding the client cannot apply is bodyless");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, resp, 3000);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client refused the 204");
    } else if (r.status != 204) {
        FAIL("the status was not 204");
    } else if (r.body_len != 0) {
        FAIL("the 204 had a body");
    } else {
        PASS();
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 7. a 204 that says chunked is not chunk-decoded ---------- */

static void test_204_that_says_chunked_is_not_decoded(void)
{
    static const char resp[] =
        "HTTP/1.1 204 No Content\r\nTransfer-Encoding: chunked\r\n\r\n";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("a 204 that says chunked is not chunk-decoded");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, resp, 3000);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client waited for a chunk that will never come");
    } else if (r.status != 204) {
        FAIL("the status was not 204");
    } else if (r.body_len != 0) {
        FAIL("the 204 had a body");
    } else {
        PASS();
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 8. the peer closes after an interim ---------- */

static void test_close_after_an_interim_is_incomplete(void)
{
    static const char resp[] = "HTTP/1.1 100 Continue\r\n\r\n";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("a peer that closes after an interim leaves it unanswered");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, resp, 100);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    errno = 0;
    if (http_client_connection_read_response(&c, &r) == 0) {
        FAIL("the client returned a response that never came");
    } else if (errno != HTTP_REASON_INCOMPLETE) {
        FAIL("the reason was not 'incomplete'");
    } else if (c.fd != -1) {
        FAIL("the connection was not closed");
    } else {
        PASS();
    }
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 9. the ordinary path is unchanged ---------- */

static void test_an_ordinary_response_still_has_its_body(void)
{
    static const char resp[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("an ordinary 200 response still has its body");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, resp, 3000);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not return the response");
    } else if (r.status != 200) {
        FAIL("the status was not 200");
    } else if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body did not arrive");
    } else {
        PASS();
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 10. the 204 leaves the connection in sync ---------- */

static void test_204_leaves_the_connection_in_sync(void)
{
    static const char resp[] =
        "HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n";
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("a 204 leaves the connection ready for the next request");
    alarm(10);
    port = spawn_two_turn_peer(&pid, resp);
    fd = connect_and_write(port, req_get);
    if (fd < 0) {
        FAIL("could not connect");
        alarm(0);
        return;
    }
    c.fd = fd;
    c.port = port;
    memset(c.host, 0, sizeof(c.host));
    http_response_parser_init(&c.parser);
    c.chunked_open = 0;
    c.n_inflight = 0;
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not return the 204");
    } else if (r.status != 204) {
        FAIL("the status was not 204");
    } else {
        /* The second turn on the same socket. */
        if (http_client_connection_write_request(&c, "GET", "/", NULL, 0) != 0) {
            FAIL("the second request could not be written");
        } else if (http_client_connection_read_response(&c, &r) != 0) {
            FAIL("the second response did not arrive");
        } else if (r.status != 200 || r.body_len != 2) {
            FAIL("the second response was wrong");
        } else {
            PASS();
        }
    }
    http_client_connection_close(&c);
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* ---------- 11. the one-shot client skips an interim too ---------- */

static void test_one_shot_skips_an_interim(void)
{
    static const char resp[] =
        "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
    sock_listener l;
    pid_t pid;
    uint16_t port;
    http_response r;

    TEST("the one-shot client reads past an interim response");
    alarm(10);
    if (sock_listen_loopback(&l, 0, 8) != 0) {
        perror("sock_listen_loopback");
        FAIL("could not listen");
        alarm(0);
        return;
    }
    port = l.port;
    pid = fork();
    if (pid < 0) {
        perror("fork");
        FAIL("could not fork");
        alarm(0);
        return;
    }
    if (pid == 0) {
        int fd = sock_accept(&l);
        if (fd >= 0) {
            char buf[1024];
            ssize_t n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, resp, strlen(resp));
            usleep(200000);
            sock_close(fd);
        }
        _exit(0);
    }
    if (http_client_get("127.0.0.1", port, "/", &r) != 0) {
        FAIL("the one-shot client did not return a response");
    } else if (r.status != 200) {
        FAIL("the one-shot client answered with the interim");
    } else if (r.body_len != 2) {
        FAIL("the one-shot client lost the body");
    } else {
        PASS();
    }
    alarm(0);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);
    signal(SIGPIPE, SIG_IGN);

    printf("Stage 65.31 -- the status decides: 1xx, 204 and 304 have no body\n");

    test_204_with_a_length_has_no_body();
    test_304_with_a_length_has_no_body();
    test_1xx_is_interim();
    test_interim_keeps_the_head_flag();
    test_204_with_an_invalid_length_is_complete();
    test_204_with_an_unknown_coding_has_no_body();
    test_204_that_says_chunked_is_not_decoded();
    test_close_after_an_interim_is_incomplete();
    test_an_ordinary_response_still_has_its_body();
    test_204_leaves_the_connection_in_sync();
    test_one_shot_skips_an_interim();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
