/*
 * Stage 65.28 -- the response to HEAD.
 *
 * The rules:
 *
 * RFC 9110 9.3.2:
 *
 *   "The HEAD method is identical to GET except that the server MUST NOT
 *   send content in the response."
 *
 *   "The server SHOULD send the same header fields in response to a HEAD
 *   request as it would have sent if the request method had been GET."
 *
 *   "Although request message framing is independent of the method used,
 *   content received in a HEAD request has no generally defined
 *   semantics, cannot alter the meaning or target of the request, and
 *   might lead some implementations to reject the request and close the
 *   connection because of its potential as a request smuggling attack."
 *
 * RFC 9112 6.3 item 1 -- the FIRST item of the precedence list:
 *
 *   "Any response to a HEAD request and any response with a 1xx
 *   (Informational), 204 (No Content), or 304 (Not Modified) status code
 *   is always terminated by the first empty line after the header fields,
 *   regardless of the header fields present in the message, and thus
 *   cannot contain a message body or trailer section."
 *
 * RFC 9110 8.6:
 *
 *   "A server MAY send a Content-Length header field in a response to a
 *   HEAD request (Section 9.3.2); a server MUST NOT send Content-Length
 *   in such a response unless its field value equals the decimal number
 *   of octets that would have been sent in the content of a response if
 *   the same request had used the GET method."
 *
 * Two halves, and they are different halves:
 *
 *   - The SERVER sends the head it would have sent for the matching GET,
 *     and no content. The Content-Length in that head describes the body
 *     the GET would have returned -- it is a claim about another message,
 *     not about this one.
 *
 *   - The CLIENT does not wait for a body that will never come. A client
 *     that waits for the bytes the length promises will sit on a
 *     connection until the peer times it out: the length is a claim
 *     about the GET, and the bytes are not coming.
 *
 * What shipped before this stage: neither half. The server answered a
 * HEAD request with the body a GET would have returned, and the client
 * waited for a body that the server never sent.
 *
 * The method token is case-sensitive (RFC 9110 9.1: "The method token is
 * case-sensitive because it might be used as a gateway to object-based
 * systems with case-sensitive method names"), so "head" is not HEAD.
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

static const char our_body[] = "hello from our server\n";

/* ---------- helpers ---------- */

static int contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

/* Does the buffer end with the empty line that ends a header section? */
static int ends_with_head(const char *buf, size_t n)
{
    if (n < 4) {
        return 0;
    }
    return memcmp(buf + n - 4, "\r\n\r\n", 4) == 0;
}

/* Our own server, for the request side of the rule. */
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

/* Send raw request bytes and read everything the server sends back
 * until it closes. Returns the number of bytes read, or -1. */
static ssize_t raw_exchange(uint16_t port, const char *request,
                             char *out, size_t out_cap)
{
    int fd = sock_connect_loopback(port);
    size_t req_len = strlen(request);
    size_t sent = 0;
    size_t got = 0;

    if (fd < 0) {
        return -1;
    }
    while (sent < req_len) {
        ssize_t n = write(fd, request + sent, req_len - sent);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            sock_close(fd);
            return -1;
        }
        sent += (size_t)n;
    }
    for (;;) {
        ssize_t n;
        if (got + 1 >= out_cap) {
            break;
        }
        n = read(fd, out + got, out_cap - got - 1);
        if (n > 0) {
            got += (size_t)n;
            continue;
        }
        if (n == 0) {
            break;              /* the server closed: the exchange is over */
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }
    out[got] = '\0';
    sock_close(fd);
    return (ssize_t)got;
}

/* A peer that sends a canned response and then HOLDS the connection
 * open for hold_ms. A client that waits for a body that will never
 * come sits here until the peer hangs up -- which is how a test can
 * tell "the client returned" from "the client waited". */
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

static double now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/* ---------- the server's half ---------- */

/* 1. HEAD /: the head the GET would have sent, and no content. */
static void test_head_returns_the_head_and_no_body(void)
{
    char req[256];
    char resp[4096];
    char want[64];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("HEAD / returns the head and no body");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "HEAD / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the status is not 200");
        return;
    }
    snprintf(want, sizeof(want), "Content-Length: %zu\r\n", strlen(our_body));
    if (!contains(resp, want)) {
        FAIL("the length is not the one the GET would have carried");
        return;
    }
    if (!ends_with_head(resp, (size_t)n)) {
        FAIL("content was sent after the head");
        return;
    }
    PASS();
}

/* 2. HEAD on a path that is not there: the 404 the GET would have
 *    gotten, without its body. */
static void test_head_of_a_missing_path_is_a_404_without_a_body(void)
{
    char req[256];
    char resp[4096];
    char want[64];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("HEAD of a missing path is a 404 without a body");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "HEAD /nope HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 404 Not Found\r\n", 24) != 0) {
        FAIL("the status is not 404");
        return;
    }
    snprintf(want, sizeof(want), "Content-Length: %zu\r\n",
             strlen("404 Not Found\n"));
    if (!contains(resp, want)) {
        FAIL("the length is not the one the GET would have carried");
        return;
    }
    if (!ends_with_head(resp, (size_t)n)) {
        FAIL("content was sent after the head");
        return;
    }
    PASS();
}

/* 3. HEAD /stream: the chunked head, and no chunks. The head says the
 *    body is chunked; the body is not there, and that is not a
 *    truncated body -- item 1 of the precedence list comes first. */
static void test_head_of_a_chunked_route_is_the_head_alone(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("HEAD of a chunked route is the head alone");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "HEAD /stream HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the status is not 200");
        return;
    }
    if (!contains(resp, "Transfer-Encoding: chunked\r\n")) {
        FAIL("the coding is missing from the head");
        return;
    }
    if (contains(resp, "Content-Length")) {
        FAIL("a chunked response must not carry a length");
        return;
    }
    if (contains(resp, "Hello")) {
        FAIL("the chunked content was sent");
        return;
    }
    if (!ends_with_head(resp, (size_t)n)) {
        FAIL("something was sent after the head");
        return;
    }
    PASS();
}

/* 4. HEAD /echo: the content of a HEAD request has no generally
 *    defined semantics (RFC 9110 9.3.2), so the response is the one
 *    the matching GET would have produced -- and a GET carries no
 *    content, so the echo is empty and the length is zero. */
static void test_head_ignores_the_content_of_the_request(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("HEAD ignores the content of the request");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "HEAD /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n"
             "Content-Length: 5\r\nConnection: close\r\n\r\nhello");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the status is not 200");
        return;
    }
    if (!contains(resp, "Content-Length: 0\r\n")) {
        FAIL("the length is not the one the matching GET would have carried");
        return;
    }
    if (contains(resp, "hello")) {
        FAIL("the request's content came back");
        return;
    }
    if (!ends_with_head(resp, (size_t)n)) {
        FAIL("content was sent after the head");
        return;
    }
    PASS();
}

/* 5. The connection is still a conversation: a GET after a HEAD on the
 *    same socket gets its own body, and the HEAD's response did not
 *    eat a single byte of it. */
static void test_a_get_after_a_head_still_has_its_body(void)
{
    char req[512];
    char resp[8192];
    char *first;
    char *second;
    char *head_end;
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("a GET after a HEAD still has its body");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "HEAD / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
             "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    first = strstr(resp, "HTTP/1.1 200 OK\r\n");
    if (first == NULL) {
        FAIL("the first response is missing");
        return;
    }
    second = strstr(first + 1, "HTTP/1.1 200 OK\r\n");
    if (second == NULL) {
        FAIL("the second response is missing");
        return;
    }
    head_end = strstr(first, "\r\n\r\n");
    if (head_end == NULL) {
        FAIL("the first response has no head");
        return;
    }
    if (head_end + 4 != second) {
        FAIL("the first response carried content");
        return;
    }
    if (!contains(second, our_body)) {
        FAIL("the GET's body is missing");
        return;
    }
    PASS();
}

/* 6. The method token is case-sensitive (RFC 9110 9.1): "head" is not
 *    HEAD, so it may have a body. */
static void test_the_method_is_case_sensitive(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("the method is case-sensitive: head is not HEAD");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "head / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 501 Not Implemented\r\n", 29) != 0) {
        /* Stage 65.29 changed what happens to a method the server does
         * not know: "head" is not a method this server recognizes, so
         * it is a 501 (RFC 9110 15.6.2), not a served document. The
         * claim this test makes -- the method token is case-sensitive,
         * so "head" is not HEAD -- is now proved in a plainer way: the
         * request was refused outright, so it was certainly not treated
         * as a HEAD. */
        FAIL("a method that is not HEAD is not served at all");
        return;
    }
    PASS();
}

/* ---------- the client's half ---------- */

/* 7. The client does not wait for a body that will never come. The
 *    peer holds the connection open for a second and a bit; a client
 *    that waits sits there until the peer hangs up. */
static void test_the_client_does_not_wait_for_a_head_body(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    double t0;
    double elapsed;

    TEST("the client does not wait for a body that will never come");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, response, 1200);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "HEAD", "/", NULL, 0) != 0) {
        FAIL("the request was not written");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    t0 = now_ms();
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not accept the response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    elapsed = now_ms() - t0;
    if (r.body_len != 0) {
        FAIL("the response carried a body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.content_length != 5) {
        FAIL("the length in the head was not reported");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (elapsed > 500.0) {
        FAIL("the client waited for the body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 8. A server that sends content after a HEAD response breaks the
 *    rule -- and the client does not read it as the body: the bytes
 *    are the start of the next message. */
static void test_content_after_a_head_response_is_not_the_body(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;

    TEST("content after a HEAD response is not the body");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, response, 200);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "HEAD", "/", NULL, 0) != 0) {
        FAIL("the request was not written");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not accept the response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != 0) {
        FAIL("the content was taken as the body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_response_parser_pending(&c.parser) != 5) {
        FAIL("the content was not kept as the next message");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 9. Item 1 comes FIRST in the precedence list: a response to HEAD is
 *    terminated by the end of the head "regardless of the header fields
 *    present in the message". So even a length that cannot be used
 *    does not make the message incomplete -- there is no body to be
 *    incomplete. */
static void test_an_unusable_length_on_a_head_response_is_still_complete(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: abc\r\n"
        "\r\n";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;

    TEST("an unusable length on a HEAD response is still complete");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, response, 200);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "HEAD", "/", NULL, 0) != 0) {
        FAIL("the request was not written");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client refused a response with no body to frame");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != 0) {
        FAIL("the response carried a body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 10. The same for a chunked response: the head says chunked, and the
 *     terminal chunk is not coming -- but there is no body to be
 *     incomplete, because the response ends at the head. */
static void test_a_chunked_head_response_has_no_body_either(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;

    TEST("a chunked HEAD response has no body either");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, response, 200);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "HEAD", "/", NULL, 0) != 0) {
        FAIL("the request was not written");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client waited for the terminal chunk");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != 0) {
        FAIL("the response carried a body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 11. The two halves together: our client asks our server for a HEAD,
 *     and gets the head -- with the length the GET would have carried,
 *     and no body. */
static void test_our_client_asks_our_server_for_a_head(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    struct timeval tv;

    TEST("our client asks our server for a HEAD");
    alarm(10);
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    /* A read timeout, so a client that waits for a body fails the test
     * with a message instead of hanging until the alarm. */
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    (void)setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (http_client_connection_request(&c, "HEAD", "/", NULL, 0, &r) != 0) {
        FAIL("the client did not get the response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.status != 200) {
        FAIL("the status is not 200");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != 0) {
        FAIL("the response carried a body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.content_length != (long)strlen(our_body)) {
        FAIL("the length is not the one the GET would have carried");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);
    alarm(90);

    test_head_returns_the_head_and_no_body();
    test_head_of_a_missing_path_is_a_404_without_a_body();
    test_head_of_a_chunked_route_is_the_head_alone();
    test_head_ignores_the_content_of_the_request();
    test_a_get_after_a_head_still_has_its_body();
    test_the_method_is_case_sensitive();
    test_the_client_does_not_wait_for_a_head_body();
    test_content_after_a_head_response_is_not_the_body();
    test_an_unusable_length_on_a_head_response_is_still_complete();
    test_a_chunked_head_response_has_no_body_either();
    test_our_client_asks_our_server_for_a_head();

    printf("\n%zu/%zu tests passed\n", (size_t)tests_passed, (size_t)tests_run);
    return tests_failed == 0 ? 0 : 1;
}
