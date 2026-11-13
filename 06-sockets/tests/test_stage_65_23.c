/*
 * Stage 65.23 -- the server answers 501 to a request whose transfer
 * coding it does not understand.
 *
 * RFC 9112 6.1, one sentence:
 *
 *   "A server that receives a request message with a transfer coding it
 *   does not understand SHOULD respond with 501 (Not Implemented)."
 *
 * The coding header is a LIST of codings, and each one in the list is
 * applied to the body in turn. "gzip, chunked" means: the content was
 * compressed, and then the compressed content was chunked. We know how
 * to read the chunked coding -- that is what Stage 65.11's decoder is
 * for -- and we do not know how to decompress gzip. So today we frame
 * the body correctly, hand back the bytes, and call them the body.
 *
 * That is the failure this stage closes. A body that is still gzip
 * coded, returned with a 200, is a server that claims to have
 * processed a message it did not understand. The bytes are the right
 * bytes and the answer is the wrong answer: the client asked for a
 * document and got a compressed stream wearing the document's name.
 *
 * Note the shape of the rule: the request is not BAD, the server is
 * just not CAPABLE. 400 says "this request is wrong"; 501 says "this
 * request is fine and I cannot do it". That is why the status differs
 * from the two refusals before it, even though all three answer and
 * close.
 *
 * The ORDER of the rules matters, and the order comes from the RFCs:
 *
 *   1. a coding AND a length            -> 400  (RFC 9112 6.3 item 3)
 *   2. final coding is not chunked      -> 400  (RFC 9112 6.3 item 4, MUST)
 *   3. a coding we do not understand    -> 501  (RFC 9112 6.1,     SHOULD)
 *
 * A MUST outranks a SHOULD, so rule 2 is checked before rule 3:
 * "Transfer-Encoding: gzip" is a 400 (the body length cannot be
 * determined reliably -- a MUST), not a 501 (gzip is a coding we do
 * not understand -- a SHOULD). "Transfer-Encoding: gzip, chunked" is
 * a 501: the framing is determinate, and the coding in front of the
 * chunked one is one we cannot apply.
 *
 * The oracle is not us: curl is the live check for the paths curl can
 * produce, and a raw socket is the oracle for the ones curl will not
 * produce on purpose. RFC 9112 6.1 is the rule the tests quote.
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

/* 1. A coding we do not understand is refused: 501 Not Implemented.
 *    The request is well formed and the framing is determinate -- the
 *    server simply cannot apply the gzip coding -- so the answer is
 *    "I cannot do this", not "this is wrong". */
static void test_a_coding_we_do_not_understand_is_refused(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: gzip, chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    static const char refused[] = "501 Not Implemented\n";

    TEST("a coding we do not understand is refused");
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
    if (r.status != 501) {
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
 *    connection is closed. A server that cannot process the message
 *    cannot be trusted with a second one. */
static void test_the_refusal_closes_the_connection(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    http_response r2;
    const char *conn_hdr;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: gzip, chunked\r\n"
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
    /* The deadline is here so that a server which waits for a body
     * becomes a failure message in this test, not a killed binary. */
    {
        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        if (setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
            FAIL("the deadline could not be set");
            http_client_connection_close(&c);
            waitpid(pid, NULL, 0);
            return;
        }
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the read failed");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (r.status != 501) {
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

/* 3. The refusal does not wait for the body. The coding is in the HEAD,
 *    so the head alone is enough to know that the server cannot apply
 *    it -- and the answer must not wait for bytes it will never be
 *    able to hand back decoded. */
static void test_the_refusal_does_not_wait_for_the_body(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    struct timeval tv;
    static const char head[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: gzip, chunked\r\n"
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
    if (r.status != 501) {
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
 *    becomes a request, and the reason is carried out of the parser so
 *    the server can choose the status. The reason is NOT a protocol
 *    error -- the request is well formed -- it is "not implemented". */
static void test_the_parser_refuses_the_request(void)
{
    http_parser p;
    http_request req;
    static const char raw[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: gzip, chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
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
        FAIL("the parser handed back a request it cannot decode");
        return;
    }
    if (errno != ENOTSUP) {
        FAIL("the refusal is not a not-implemented error");
        return;
    }
    PASS();
}

/* 5. A MUST outranks a SHOULD. "chunked, gzip" and "gzip" both name a
 *    final coding that is not chunked, so RFC 9112 6.3 item 4 applies
 *    -- a MUST -- and the answer is 400, not the 501 that 6.1's SHOULD
 *    would allow. The rules are checked in the order the RFCs put
 *    them in. */
static void test_the_must_outranks_the_should(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    static const char not_final[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: chunked, gzip\r\n"
        "Connection: close\r\n"
        "\r\n";
    static const char alone[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: gzip\r\n"
        "Connection: close\r\n"
        "\r\n";

    TEST("the MUST outranks the SHOULD");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, not_final, sizeof(not_final) - 1) != 0) {
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
        FAIL("a request whose length cannot be determined was not a 400");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);

    /* And the same for a coding that is alone. */
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, alone, sizeof(alone) - 1) != 0) {
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
        FAIL("a coding alone was not a 400");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 6. A coding we DO understand is still answered: the rule is about the
 *    codings we cannot apply, not about requests. */
static void test_a_coding_we_understand_is_still_answered(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    static const char req[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";

    TEST("a coding we understand is still answered");
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
        FAIL("a chunked request was refused");
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

/* 7. A coding's name is case-insensitive, in both directions:
 *    "GZIP" is gzip, and "CHUNKED" is chunked. */
static void test_the_coding_s_name_is_case_insensitive(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    static const char unknown[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: GZIP, CHUNKED\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    static const char known[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: CHUNKED\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";

    TEST("the coding's name is case-insensitive");
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, unknown, sizeof(unknown) - 1) != 0) {
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
    if (r.status != 501) {
        FAIL("an unknown coding in capitals was answered");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);

    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    if (write_raw(&c, known, sizeof(known) - 1) != 0) {
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
        FAIL("a known coding in capitals was refused");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 8. A list may carry EMPTY elements -- RFC 9110 5.6.1: a recipient
 *    "MUST accept lists that satisfy the following syntax:
 *    #element => [ element ] *( OWS "," OWS [ element ] )". So an empty
 *    element is not a coding: "gzip, , chunked" names two codings,
 *    gzip and chunked, and gzip is one we cannot apply -- while
 *    ", chunked" names one coding, chunked, and the request is framed
 *    by chunks and answered. */
static void test_an_empty_list_element_is_ignored(void)
{
    http_parser p;
    http_request req;
    static const char unknown[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: gzip, , chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    static const char known[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Transfer-Encoding: , chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n\r\n";
    http_parse_status st;

    TEST("an empty list element is ignored");
    http_parser_init(&p);
    if (http_parser_feed(&p, unknown, sizeof(unknown) - 1) != 0) {
        FAIL("the feed failed");
        return;
    }
    errno = 0;
    st = http_parser_next(&p, &req);
    if (st != HTTP_PARSE_ERROR || errno != ENOTSUP) {
        FAIL("an unknown coding among empty elements was accepted");
        return;
    }

    http_parser_init(&p);
    if (http_parser_feed(&p, known, sizeof(known) - 1) != 0) {
        FAIL("the feed failed");
        return;
    }
    st = http_parser_next(&p, &req);
    if (st != HTTP_PARSE_OK) {
        FAIL("an empty element was read as a coding");
        return;
    }
    if (req.body_len != 5 || memcmp(req.body, "hello", 5) != 0) {
        FAIL("the body is not the chunk-decoded body");
        return;
    }
    PASS();
}

/* 9. A plain request is still answered: the rule is about a coding we
 *    cannot apply, not about requests. */
static void test_a_plain_request_is_still_answered(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;
    static const char req[] =
        "GET / HTTP/1.1\r\n"
        "Host: test\r\n"
        "Connection: close\r\n"
        "\r\n";

    TEST("a plain request is still answered");
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
    printf("Stage 65.23 -- the server answers 501 to a request whose "
           "transfer coding it does not understand\n\n");

    alarm(30);
    test_a_coding_we_do_not_understand_is_refused();
    test_the_refusal_closes_the_connection();
    test_the_refusal_does_not_wait_for_the_body();
    test_the_parser_refuses_the_request();
    test_the_must_outranks_the_should();
    test_a_coding_we_understand_is_still_answered();
    test_the_coding_s_name_is_case_insensitive();
    test_an_empty_list_element_is_ignored();
    test_a_plain_request_is_still_answered();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
