/*
 * Stage 65.22 -- the server refuses a request whose final coding is not
 * chunked.
 *
 * RFC 9112 6.3 says how a message's body length is determined, and item 4
 * has three sentences that answer three different questions:
 *
 *   1. "If a Transfer-Encoding header field is present and the chunked
 *      transfer coding is the final encoding, the message body length is
 *      determined by reading and decoding the chunked data until the
 *      transfer coding indicates the data is complete."        <- 65.19
 *
 *   2. "... in a response and the chunked transfer coding is not the
 *      final encoding, the message body length is determined by reading
 *      the connection until it is closed by the server."   <- a client rule
 *
 *   3. "... in a REQUEST and the chunked transfer coding is not the
 *      final encoding, the message body length cannot be determined
 *      reliably; the server MUST respond with the 400 (Bad Request)
 *      status code and then close the connection."       <- THIS STAGE
 *
 * The third sentence is a MUST, and it is the one we had not implemented.
 * A request with "Transfer-Encoding: gzip" asks a question the server
 * cannot answer -- where does the body end? -- and a server that guesses
 * is the server that disagrees with the next hop about the same thing.
 * That disagreement is request smuggling (RFC 9112 11.2), and Stage
 * 65.21 closed the other half of it: two framings (a coding AND a
 * length) are refused, because a request with two answers to the same
 * question is the same shape with one more header.
 *
 * The rule is about the FINAL coding, and that word carries the whole
 * rule. "gzip, chunked" frames the body with chunks -- the coding before
 * it is applied first, and the chunked coding draws the boundary -- so
 * that request is answered. "chunked, gzip" does not: the chunked coding
 * is not the final encoding, the body length cannot be determined, and
 * the server MUST refuse. The same two codings in the other order give
 * the opposite answer, which is exactly what "final" means.
 *
 * Two RFCs are quoted here, and the second one matters as much as the
 * first:
 *
 *   - RFC 9112 6.3 item 4 -- the MUST (above).
 *   - RFC 9110 5.6.1 -- a recipient "MUST accept lists that satisfy the
 *     following syntax: #element => [ element ] *( OWS "," OWS [ element ] )"
 *     -- so a list may carry EMPTY elements, and a trailing comma does
 *     not make the final coding empty. "chunked," names one coding:
 *     chunked. A rule that fires on the final coding has to get the
 *     final coding right, or it refuses requests the RFC says to accept.
 *
 * The oracle is not us: curl is the live check for the paths that curl
 * can produce, and a raw socket is the oracle for the ones curl will
 * not produce on purpose. RFC 9112 6.3 is the rule the tests quote.
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

/* Write raw bytes on the client's socket: the request is captured wire
 * traffic, not something our own builder would have written. */
static int write_raw(http_client_connection *c, const void *bytes, size_t len)
{
    return sock_write_once(c->fd, bytes, len) == (ssize_t)len ? 0 : -1;
}

/* 1. A request whose final coding is not chunked is refused: 400, and
 *    the body is NOT the echo. The coding names a body we cannot
 *    measure, so the server says no instead of guessing. */
static void test_a_final_coding_that_is_not_chunked_is_refused(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: gzip\r\n"
        "Connection: close\r\n"
        "\r\n"
        "compressed bytes";
    static const char refused[] = "400 Bad Request\n";

    TEST("a final coding that is not chunked is refused");
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
 *    connection is closed. A connection whose body length cannot be
 *    determined cannot be trusted with a second message. */
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
        "Transfer-Encoding: gzip\r\n"
        "Connection: close\r\n"
        "\r\n";

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

/* 3. The refusal does not wait for the body. The framing is unusable
 *    from the HEAD alone, so the server must not sit there waiting for
 *    bytes it cannot measure. */
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
        "Transfer-Encoding: gzip\r\n"
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
static void test_the_parser_refuses_the_request(void)
{
    http_parser p;
    http_request req;
    static const char raw[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: gzip\r\n"
        "\r\n"
        "compressed bytes";
    http_parse_status st;

    TEST("the parser refuses the request");
    http_parser_init(&p);
    if (http_parser_feed(&p, raw, sizeof(raw) - 1) != 0) {
        FAIL("the feed failed");
        return;
    }
    errno = 0;
    st = http_parser_next(&p, &req);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("the parser handed back a request it cannot frame");
        return;
    }
    if (errno != EPROTO) {
        FAIL("the refusal is not a protocol error");
        return;
    }
    PASS();
}

/* 5. The rule is about the FINAL coding. The same two codings in the
 *    other order give the opposite answer: "chunked, gzip" has the
 *    chunked coding NOT final, so the body length cannot be determined
 *    and the server MUST refuse. */
static void test_the_final_coding_is_the_last_one_not_the_first(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: chunked, gzip\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";

    TEST("the final coding is the last one, not the first");
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
        FAIL("a request whose final coding is not chunked was answered");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 6. A list may carry EMPTY elements -- RFC 9110 5.6.1: a recipient
 *    "MUST accept lists that satisfy the following syntax:
 *    #element => [ element ] *( OWS "," OWS [ element ] )". So a
 *    trailing comma does not make the final coding empty: "chunked,"
 *    names one coding, chunked, and the request is framed by chunks.
 *
 *    A rule that fires on the final coding has to get the final coding
 *    right, or it refuses requests the RFC says to accept. */
static void test_an_empty_list_element_is_ignored(void)
{
    http_parser p;
    http_request req;
    static const char raw[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: chunked,\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    http_parse_status st;

    TEST("an empty list element is ignored");
    http_parser_init(&p);
    if (http_parser_feed(&p, raw, sizeof(raw) - 1) != 0) {
        FAIL("the feed failed");
        return;
    }
    st = http_parser_next(&p, &req);
    if (st != HTTP_PARSE_OK) {
        FAIL("a chunked-final request with a trailing comma was refused");
        return;
    }
    if (req.body_len != 5 || memcmp(req.body, "hello", 5) != 0) {
        FAIL("the body is not the chunk-decoded body");
        return;
    }
    PASS();
}

/* 7. A coding's name is case-insensitive: "Chunked" is chunked. */
static void test_the_coding_s_name_is_case_insensitive(void)
{
    http_parser p;
    http_request req;
    static const char raw[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: Chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    http_parse_status st;

    TEST("the coding's name is case-insensitive");
    http_parser_init(&p);
    if (http_parser_feed(&p, raw, sizeof(raw) - 1) != 0) {
        FAIL("the feed failed");
        return;
    }
    st = http_parser_next(&p, &req);
    if (st != HTTP_PARSE_OK) {
        FAIL("a chunked request was refused");
        return;
    }
    if (req.body_len != 5 || memcmp(req.body, "hello", 5) != 0) {
        FAIL("the body is not the chunk-decoded body");
        return;
    }
    PASS();
}

/* 8. An empty Transfer-Encoding is refused: a header with no value is
 *    not a header, and it is refused by the header parser before the
 *    coding rule applies. "Present but empty" is not "absent". */
static void test_an_empty_transfer_encoding_is_refused(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding:\r\n"
        "Connection: close\r\n"
        "\r\n";

    TEST("an empty Transfer-Encoding is refused");
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
        FAIL("a header with no value was answered");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 9. A length alone is still answered: the rule is narrow. */
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

/* 10. No coding at all is still answered: the rule is about a coding
 *     that names something we cannot frame, not about requests. */
static void test_no_coding_at_all_is_still_answered(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    static const char req[] =
        "GET / HTTP/1.1\r\n"
        "Host: test\r\n"
        "Connection: close\r\n"
        "\r\n";

    TEST("no coding at all is still answered");
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
        FAIL("a plain request was refused");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.body_len != sizeof(our_body) - 1 ||
        memcmp(r.body, our_body, r.body_len) != 0) {
        FAIL("the answer is not the document");
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
    printf("Stage 65.22 -- the server refuses a request whose final coding "
           "is not chunked\n\n");

    alarm(30);
    test_a_final_coding_that_is_not_chunked_is_refused();
    test_the_refusal_closes_the_connection();
    test_the_refusal_does_not_wait_for_the_body();
    test_the_parser_refuses_the_request();
    test_the_final_coding_is_the_last_one_not_the_first();
    test_an_empty_list_element_is_ignored();
    test_the_coding_s_name_is_case_insensitive();
    test_an_empty_transfer_encoding_is_refused();
    test_a_length_alone_is_still_answered();
    test_no_coding_at_all_is_still_answered();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
