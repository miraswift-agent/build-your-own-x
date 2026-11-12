/*
 * Stage 65.27 -- the truncated body.
 *
 * The rule: RFC 9112 6.3 item 6 --
 *
 *   "If a valid Content-Length header field is present without
 *   Transfer-Encoding, its decimal value defines the expected message
 *   body length in octets. If the sender closes the connection or the
 *   recipient times out before the indicated number of octets are
 *   received, the recipient MUST consider the message to be incomplete
 *   and close the connection."
 *
 * and RFC 9112 8, which gives the same rule its vocabulary:
 *
 *   "A client that receives an incomplete response message, which can
 *   occur when a connection is closed prematurely or when decoding a
 *   supposedly chunked transfer coding fails, MUST record the message
 *   as incomplete."
 *
 *   "A message body that uses the chunked transfer coding is incomplete
 *   if the zero-sized chunk that terminates the encoding has not been
 *   received. A message that uses a valid Content-Length is incomplete
 *   if the size of the message body received (in octets) is less than
 *   the value given by Content-Length. A response that has neither
 *   chunked transfer coding nor Content-Length is terminated by closure
 *   of the connection and, if the header section was received intact, is
 *   considered complete."
 *
 * That last sentence is the one that keeps the rule narrow: a response
 * with NO framing is COMPLETE when the peer closes. The rule fires on a
 * body that was promised and did not arrive, not on a close.
 *
 * What shipped before this stage: the client DETECTED the truncation
 * (http_response_parser_finish reported an error) but reported it as
 * EPROTO -- a malformed message -- and left the connection OPEN. The
 * MUST is the close, and the word is "incomplete", which is neither
 * malformed nor invalid framing.
 *
 * The server's half was already right by construction: the request
 * parser waits for the whole body (NEED_MORE, not an error), so an
 * incomplete request never becomes a request and is never served. This
 * file pins that with a test rather than changing it.
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

/* A peer that sends a canned response and then closes the connection.
 * It reads the request first: a peer that hangs up with an unread
 * request sends an RST, and the RST discards the bytes it already
 * sent. */
static uint16_t spawn_canned_peer(pid_t *pid_out, const char *response)
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
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

/* A peer that sends a canned response and then waits for the client to
 * hang up. It exits 0 if it saw EOF -- which is how a test knows the
 * client closed the connection -- and 1 otherwise. */
static uint16_t spawn_holding_peer(pid_t *pid_out, const char *response)
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
        int code = 1;
        if (fd >= 0) {
            char buf[1024];
            ssize_t n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, response, strlen(response));
            for (;;) {
                n = read(fd, buf, sizeof(buf));
                if (n == 0) {
                    code = 0;    /* EOF: the client closed the connection */
                    break;
                }
                if (n < 0) {
                    break;
                }
            }
            sock_close(fd);
        }
        _exit(code);
    }
    *pid_out = pid;
    return l.port;
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

/* Ask a peer for a response. Returns 0 when the client read a
 * response, -1 when the read failed. */
static int ask_peer(uint16_t port, http_client_connection *c, http_response *r)
{
    if (http_client_connection_open(c, "127.0.0.1", port) != 0) {
        return -1;
    }
    if (http_client_connection_write_request(c, "GET", "/", NULL, 0) != 0) {
        return -1;
    }
    return http_client_connection_read_response(c, r);
}

/* 1. THE RULE, on the client. The response says the body is ten
 *    octets; three arrive and the peer closes. The message is
 *    incomplete, so the client MUST NOT deliver it -- and the client
 *    MUST close the connection (RFC 9112 6.3 item 6). */
static void test_the_client_refuses_a_body_shorter_than_the_length(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "abc";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;

    TEST("the client refuses a body shorter than the length");
    alarm(10);
    port = spawn_canned_peer(&pid, response);
    errno = 0;
    if (ask_peer(port, &c, &r) == 0) {
        FAIL("the client accepted a body that never arrived");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno != HTTP_REASON_INCOMPLETE) {
        FAIL("the reason is not the incomplete reason");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (c.fd != -1) {
        FAIL("the connection was left open");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 2. The close is a real close, not a field set to -1: the descriptor
 *    the client was using is gone. */
static void test_the_connection_is_really_closed(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "abc";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    int saved;

    TEST("the connection is really closed after an incomplete message");
    alarm(10);
    port = spawn_canned_peer(&pid, response);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    saved = c.fd;
    if (http_client_connection_write_request(&c, "GET", "/", NULL, 0) != 0) {
        FAIL("the request was not written");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    (void)http_client_connection_read_response(&c, &r);
    if (c.fd != -1) {
        FAIL("the connection was left open");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    errno = 0;
    if (read(saved, &r, 1) != -1 || errno != EBADF) {
        FAIL("the descriptor is still open");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 3. The CHUNKED half. A chunked body is incomplete if the zero-sized
 *    chunk that terminates the encoding has not been received
 *    (RFC 9112 8). The peer sends one data chunk and closes. */
static void test_a_chunked_body_that_stops_is_incomplete(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;

    TEST("a chunked body that stops is incomplete");
    alarm(10);
    port = spawn_canned_peer(&pid, response);
    errno = 0;
    if (ask_peer(port, &c, &r) == 0) {
        FAIL("the client accepted a body with no terminal chunk");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno != HTTP_REASON_INCOMPLETE) {
        FAIL("the reason is not the incomplete reason");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (c.fd != -1) {
        FAIL("the connection was left open");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 4. THE RULE IS NARROW. A response with neither chunked transfer
 *    coding nor Content-Length is terminated by closure of the
 *    connection, and "if the header section was received intact, is
 *    considered complete" (RFC 9112 8). The body is what arrived, and
 *    the client accepts it. */
static void test_a_close_delimited_response_is_complete(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "\r\n"
        "hello";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;

    TEST("a close-delimited response is complete");
    alarm(10);
    port = spawn_canned_peer(&pid, response);
    if (ask_peer(port, &c, &r) != 0) {
        FAIL("the client refused a close-delimited response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.status != 200) {
        FAIL("the status is not the one the peer sent");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 5. The path that must keep working: a response that arrives whole
 *    is read, and the body is the body the peer wrote. */
static void test_a_complete_response_is_unaffected(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;

    TEST("a complete response is still read");
    alarm(10);
    port = spawn_canned_peer(&pid, response);
    if (ask_peer(port, &c, &r) != 0) {
        FAIL("the client refused a complete response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* 6. THE PARSER says what happened. A body that was promised and did
 *    not arrive is incomplete -- not malformed (EPROTO) and not invalid
 *    framing (EBADMSG). */
static void test_the_parser_reports_incomplete(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "abc";
    http_response_parser p;
    http_response r;

    TEST("the parser reports an incomplete message");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, response, strlen(response)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    errno = 0;
    if (http_response_parser_finish(&p, &r) != HTTP_PARSE_ERROR) {
        FAIL("the parser accepted a message that never finished");
        return;
    }
    if (errno != HTTP_REASON_INCOMPLETE) {
        FAIL("the reason is not the incomplete reason");
        return;
    }
    PASS();
}

/* 7. The other side of the same distinction: a peer that closed
 *    without sending anything at all did not send an incomplete
 *    response -- it sent no response. That is a protocol error, and
 *    it is NOT the incomplete reason. */
static void test_the_parser_says_there_was_no_response(void)
{
    http_response_parser p;
    http_response r;

    TEST("the parser says there was no response at all");
    http_response_parser_init(&p);
    errno = 0;
    if (http_response_parser_finish(&p, &r) != HTTP_PARSE_ERROR) {
        FAIL("the parser accepted an empty message");
        return;
    }
    if (errno == HTTP_REASON_INCOMPLETE) {
        FAIL("an absent response is not an incomplete one");
        return;
    }
    if (errno != EPROTO) {
        FAIL("the reason is not the protocol reason");
        return;
    }
    PASS();
}

/* 8. A response that stops in the MIDDLE OF THE HEAD is an incomplete
 *    message (RFC 9112 8): the client cannot assume the status line
 *    means what it seems to mean. */
static void test_a_partial_head_is_incomplete(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n";
    http_response_parser p;
    http_response r;

    TEST("a head that stops is an incomplete message");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, response, strlen(response)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    errno = 0;
    if (http_response_parser_finish(&p, &r) != HTTP_PARSE_ERROR) {
        FAIL("the parser accepted a message with no end of head");
        return;
    }
    if (errno != HTTP_REASON_INCOMPLETE) {
        FAIL("the reason is not the incomplete reason");
        return;
    }
    PASS();
}

/* 9. The one-shot client reports the same reason. The reason is the
 *    evidence, so it has to survive the path that closes the socket. */
static void test_the_one_shot_client_reports_incomplete(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "abc";
    pid_t pid;
    uint16_t port;
    http_response r;

    TEST("the one-shot client reports an incomplete message");
    alarm(10);
    port = spawn_canned_peer(&pid, response);
    memset(&r, 0, sizeof(r));
    errno = 0;
    if (http_client_get("127.0.0.1", port, "/", &r) == 0) {
        FAIL("the client accepted a body that never arrived");
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno != HTTP_REASON_INCOMPLETE) {
        FAIL("the reason is not the incomplete reason");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* Send a request to our own server and close the write side, so the
 * server sees the peer go away. Returns the number of bytes the server
 * sent back (0 means it answered nothing). */
static ssize_t ask_our_server_with_short_body(const char *request, size_t len)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    char buf[512];
    ssize_t total = 0;

    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        waitpid(pid, NULL, 0);
        return -1;
    }
    if (sock_write_once(c.fd, request, len) != (ssize_t)len) {
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return -1;
    }
    /* Half-close: the server sees the peer go away, and we can still
     * read whatever it sent. */
    if (shutdown(c.fd, SHUT_WR) != 0) {
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return -1;
    }
    for (;;) {
        ssize_t n = read(c.fd, buf, sizeof(buf));
        if (n > 0) {
            total += n;
            continue;
        }
        break;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    return total;
}

/* 10. THE SERVER'S HALF. "A server that receives an incomplete request
 *     message ... MAY send an error response prior to closing the
 *     connection" (RFC 9112 8) -- MAY, and the peer is gone, so the
 *     server closes without answering. What it must NOT do is serve
 *     the partial request. */
static void test_the_server_does_not_serve_an_incomplete_request(void)
{
    static const char request[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "abc";
    ssize_t n;

    TEST("the server does not serve an incomplete request");
    alarm(10);
    n = ask_our_server_with_short_body(request, sizeof(request) - 1);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (n != 0) {
        FAIL("the server answered a request whose body never arrived");
        return;
    }
    alarm(0);
    PASS();
}

/* 11. The same, for a chunked request whose terminal chunk never
 *     arrives: the body was promised by the coding, and the coding
 *     never finished. */
static void test_the_server_does_not_serve_a_chunked_request_that_stops(void)
{
    static const char request[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello";
    ssize_t n;

    TEST("the server does not serve a chunked request that stops");
    alarm(10);
    n = ask_our_server_with_short_body(request, sizeof(request) - 1);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (n != 0) {
        FAIL("the server answered a request whose body never finished");
        return;
    }
    alarm(0);
    PASS();
}

/* 12. The rule is narrow in the other direction too: a MALFORMED
 *     response is not an incomplete one, and the client does not close
 *     the connection for it. That is a decision, not an RFC MUST (the
 *     RFCs only require the close for the framing errors), and this
 *     test keeps the stage from quietly becoming "close on every
 *     failure". */
static void test_the_client_does_not_close_on_a_malformed_response(void)
{
    static const char response[] =
        "NOT-HTTP AT ALL\r\n"
        "\r\n";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    int st = 0;

    TEST("the client does not close on a malformed response");
    alarm(10);
    port = spawn_holding_peer(&pid, response);
    errno = 0;
    if (ask_peer(port, &c, &r) == 0) {
        FAIL("the client accepted a malformed response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno == HTTP_REASON_INCOMPLETE) {
        FAIL("a malformed response is not an incomplete one");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (c.fd == -1) {
        FAIL("the connection was closed for a malformed response");
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    if (waitpid(pid, &st, 0) < 0) {
        FAIL("the peer did not finish");
        return;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        FAIL("the peer did not see the client hang up");
        return;
    }
    alarm(0);
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);
    alarm(90);

    test_the_client_refuses_a_body_shorter_than_the_length();
    test_the_connection_is_really_closed();
    test_a_chunked_body_that_stops_is_incomplete();
    test_a_close_delimited_response_is_complete();
    test_a_complete_response_is_unaffected();
    test_the_parser_reports_incomplete();
    test_the_parser_says_there_was_no_response();
    test_a_partial_head_is_incomplete();
    test_the_one_shot_client_reports_incomplete();
    test_the_server_does_not_serve_an_incomplete_request();
    test_the_server_does_not_serve_a_chunked_request_that_stops();
    test_the_client_does_not_close_on_a_malformed_response();

    printf("\n%zu/%zu tests passed\n", (size_t)tests_passed, (size_t)tests_run);
    return tests_failed == 0 ? 0 : 1;
}
