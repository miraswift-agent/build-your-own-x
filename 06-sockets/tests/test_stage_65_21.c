/*
 * Stage 65.21 -- the server refuses a request whose framing is ambiguous.
 *
 * Stage 65.19 shipped the SAFE half of the smuggling rule: when a
 * request carried both a Transfer-Encoding and a Content-Length, the
 * coding on the wire framed the body and the length was ignored. That is
 * what RFC 9112 6.3 says happens -- but the same paragraph also says
 * such a message "ought to be handled as an error", and a server that
 * only implements the first half leaves the shape open: two recipients
 * can disagree about where the body ends, and that disagreement is what
 * request smuggling is (RFC 9112 11.2).
 *
 * This stage implements the LOUD half: the server refuses. A request
 * that carries both headers gets 400 Bad Request, and the connection
 * closes -- because a connection whose framing is ambiguous cannot be
 * trusted with a second message.
 *
 * The rule is about the PRESENCE of the two headers, not about which
 * coding the Transfer-Encoding names. "Here is a coding" and "here is a
 * length" are two different answers to "where does the body end", and
 * that is the shape, whatever the coding is called.
 *
 * The role is not forgotten. The CLIENT still trusts the coding on a
 * response: the same paragraph's first sentence says the coding
 * overrides the length, and a client that cannot say no has to pick one.
 * The safe half is what a recipient that must process the message anyway
 * does; the loud half is what an origin server that CAN say no does.
 * Both are the same paragraph.
 *
 * The oracle is not us: curl is the live check, and RFC 9112 6.3 is the
 * rule the tests quote.
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
#include <sys/time.h>
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

/* Write raw bytes on the client's socket: the request is captured wire
 * traffic, not something our own builder would have written. */
static int write_raw(http_client_connection *c, const void *bytes, size_t len)
{
    return sock_write_once(c->fd, bytes, len) == (ssize_t)len ? 0 : -1;
}

/* 1. A request with both a coding and a length is refused: 400, and the
 *    body is NOT the echo. */
static void test_a_coding_and_a_length_together_are_refused(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 3\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    static const char refused[] = "400 Bad Request\n";

    TEST("a coding and a length together are refused");
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
    if (r.status != 400) {
        FAIL("the request was answered, not refused");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != sizeof(refused) - 1 ||
        memcmp(r.body, refused, r.body_len) != 0) {
        FAIL("the answer is not the refusal");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 2. The refusal ends the conversation: the answer says so, and the
 *    connection is closed. A connection whose framing is ambiguous
 *    cannot be trusted with a second message. */
static void test_the_refusal_closes_the_connection(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    http_response r2;
    const char *conn_hdr;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 3\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";

    TEST("the refusal closes the connection");
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
    if (r.status != 400) {
        FAIL("the request was answered, not refused");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    conn_hdr = http_response_header_get(&r, "Connection");
    if (conn_hdr == NULL || strcmp(conn_hdr, "close") != 0) {
        FAIL("the refusal does not say the connection is closing");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    /* A second turn on a refused connection gets nothing: the peer hung
     * up. */
    if (http_client_connection_read_response(&c, &r2) == 0) {
        FAIL("a second response arrived on a refused connection");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 3. The refusal does not wait for the body. The framing is ambiguous
 *    from the HEAD alone, so the server must not sit there waiting for
 *    chunks it has already decided to refuse. */
static void test_the_refusal_does_not_wait_for_the_body(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    struct timeval tv;
    static const char head[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 3\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n";

    TEST("the refusal does not wait for the body");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, head, sizeof(head) - 1) != 0) {
        FAIL("the write failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    /* This test is EXPECTED to hang if the server waits for a body, so
     * it gets its own deadline: a hang becomes a failure message in
     * this test, not a killed binary that takes the rest of the suite
     * with it. */
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    if (setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        FAIL("the deadline could not be set");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the read failed -- the server waited for a body it refused");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.status != 400) {
        FAIL("the answer is not the refusal");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 4. The refusal lives in the PARSER, not in a route: the request never
 *    becomes a request. The framing is what is wrong, so the framing is
 *    where the answer is. */
static void test_the_parser_refuses_the_conflicting_request(void)
{
    http_parser p;
    http_request req;
    static const char raw[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 3\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    http_parse_status st;

    TEST("the parser refuses the conflicting request");
    http_parser_init(&p);
    if (http_parser_feed(&p, raw, sizeof(raw) - 1) != 0) {
        FAIL("the feed failed");
        return;
    }
    errno = 0;
    st = http_parser_next(&p, &req);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("the parser handed back a request with two framings");
        return;
    }
    if (errno != EPROTO) {
        FAIL("the refusal is not a protocol error");
        return;
    }
    PASS();
}

/* 5. A coding alone is still answered: the rule is narrow. */
static void test_a_coding_alone_is_still_answered(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char decoded[] = "Hello, chunked world!\n";
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "7\r\nHello, \r\n"
        "f\r\nchunked world!\n\r\n"
        "0\r\n\r\n";

    TEST("a coding alone is still answered");
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
    if (r.status != 200) {
        FAIL("a request with one framing was refused");
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

/* 6. A length alone is still answered: the rule is narrow the other
 *    way too. */
static void test_a_length_alone_is_still_answered(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello";

    TEST("a length alone is still answered");
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
    if (r.status != 200) {
        FAIL("a request with one framing was refused");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the echo changed the body");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 7. A length of ZERO is still a length. The conflict is the presence
 *    of the two headers, not the size of the claim. */
static void test_a_zero_length_is_still_a_conflict(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 0\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "0\r\n\r\n";

    TEST("a zero length is still a conflict");
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
    if (r.status != 400) {
        FAIL("a zero length was not a conflict");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 8. The rule is about the PRESENCE of a coding, not about its name. A
 *    coding we do not know is a different gap (RFC 9112 6.3 item 4:
 *    400 when the final coding is not chunked); what this test pins is
 *    that two framings are refused whatever the coding is called. */
static void test_the_rule_is_about_presence_not_the_coding_s_name(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 5\r\n"
        "Transfer-Encoding: gzip\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello";

    TEST("the rule is about presence, not the coding's name");
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
    if (r.status != 400) {
        FAIL("two framings were not refused");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 9. The role is not forgotten: the CLIENT still trusts the coding on a
 *    response. The same RFC paragraph says the coding overrides the
 *    length; a client that cannot say no has to pick one, and the
 *    server -- which can -- now says no. */
static void test_the_client_still_trusts_the_coding_on_a_response(void)
{
    http_response_parser p;
    http_response r;
    static const char raw[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    http_parse_status st;

    TEST("the client still trusts the coding on a response");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, raw, sizeof(raw) - 1) != 0) {
        FAIL("the feed failed");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the response did not parse");
        return;
    }
    if (!r.framed_by_chunked || r.content_length != -1) {
        FAIL("the client did not trust the coding");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the decoded body");
        return;
    }
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);
    printf("Stage 65.21 -- the server refuses an ambiguous framing\n\n");

    alarm(30);
    test_a_coding_and_a_length_together_are_refused();
    test_the_refusal_closes_the_connection();
    test_the_refusal_does_not_wait_for_the_body();
    test_the_parser_refuses_the_conflicting_request();
    test_a_coding_alone_is_still_answered();
    test_a_length_alone_is_still_answered();
    test_a_zero_length_is_still_a_conflict();
    test_the_rule_is_about_presence_not_the_coding_s_name();
    test_the_client_still_trusts_the_coding_on_a_response();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
