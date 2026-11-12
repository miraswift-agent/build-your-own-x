/*
 * Stage 65.24 -- the client refuses a response whose transfer coding it
 * does not understand.
 *
 * The mirror of Stage 65.23. On the server side, a request that names a
 * coding the server cannot apply is answered 501: the request is fine,
 * the server is just not capable. On the client side the failure is
 * quieter and worse: today the client reads the chunks, hands the bytes
 * back, and calls them the body -- even though the coding in front of
 * the chunked one was never applied. The caller asked for a document
 * and got a compressed stream wearing the document's name.
 *
 * The RFCs are clear about what a recipient must implement, and quiet
 * about the rest:
 *
 *   "A recipient MUST be able to parse the chunked transfer coding"
 *      (RFC 9112 6.1) -- chunked is the one coding every recipient has
 *      to have, and says nothing about the others.
 *
 *   "Any recipient along the request/response chain MAY decode the
 *    received transfer coding(s) ... assuming that corresponding changes
 *    are made to the Transfer-Encoding field value" (RFC 9112 6.1)
 *      -- passing a coded body through is allowed, but only while the
 *      header still names the coding.
 *
 * This client is the END of the chain, and its API has one field for
 * the body: out.body means "the body". A body it cannot decode is not
 * a body, so the client cannot deliver it, so the client fails the
 * response -- the same reason the server used, in the same errno
 * (ENOTSUP), on the other side of the wire.
 *
 * The stage also fixes a drift that had already cost us once: the
 * client's own final-coding scan was still the OLD scan, so it read
 * "Transfer-Encoding: chunked," as "no coding" and handed back the raw
 * chunk framing as the body. The scan now lives in one place
 * (src/coding.h) and both roles read the list the same way -- the last
 * NON-EMPTY element is the final coding, because RFC 9110 5.6.1 says a
 * recipient must accept a list that contains empty elements.
 *
 * The oracle is not us: the bytes below are a real response head, and
 * the rules are RFC 9112 6.1 and RFC 9110 5.6.1.
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

/* A hang is a poor failure mode: if the client waits for a body that
 * never arrives, this fires and the test fails with a reason. */
static const char *current_test = "(none)";

#define TEST(name) do { tests_run++; current_test = name; printf("  %-62s", name); } while (0)
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

/* The chunked body of the captures below: one chunk of seven bytes and
 * the terminal chunk. */
static const char chunk_framing[] = "7\r\nHello, \r\n0\r\n\r\n";
static const char expected_body[] = "Hello, ";

/* A response head naming one coding we cannot apply. */
static const char head_gzip_chunked[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Transfer-Encoding: gzip, chunked\r\n"
    "\r\n";

/* 1. The mirror of 65.23: a response whose coding list names a coding
 *    this client cannot apply is refused, and the reason is ENOTSUP --
 *    "not implemented", the same errno the server used. */
static void test_a_coding_we_cannot_apply_is_refused(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("a coding we cannot apply is refused");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, head_gzip_chunked,
                                  strlen(head_gzip_chunked)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    if (http_response_parser_feed(&p, chunk_framing, strlen(chunk_framing)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    errno = 0;
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("the response was accepted");
        return;
    }
    if (errno != ENOTSUP) {
        FAIL("the reason is not ENOTSUP");
        return;
    }
    PASS();
}

/* 2. The refusal happens in the HEAD, before any body is framed: the
 *    client must not wait for bytes it has already decided to refuse.
 *    (The server's rule, from 65.22: the answer does not wait for a
 *    body we cannot measure.) */
static void test_the_refusal_happens_in_the_head(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("the refusal happens in the head, before any body is framed");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, head_gzip_chunked,
                                  strlen(head_gzip_chunked)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    errno = 0;
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("the head alone did not produce the refusal");
        return;
    }
    if (errno != ENOTSUP) {
        FAIL("the reason is not ENOTSUP");
        return;
    }
    PASS();
}

/* 3. The refusal does not hand back the coded bytes: out.body means
 *    "the body", and a body the client cannot decode is not one.
 *
 *    The struct is cleared first: the client's parser does not zero its
 *    output on the error path (the server's does -- http_parser_next
 *    memsets the request), so a caller must not read a response the
 *    parser refused. The claim here is about what the parser WROTE. */
static void test_the_refusal_hands_back_no_body(void)
{
    http_response_parser p;
    http_response r;

    TEST("the refusal hands back no body");
    memset(&r, 0, sizeof(r));
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, head_gzip_chunked,
                                  strlen(head_gzip_chunked)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    if (http_response_parser_feed(&p, chunk_framing, strlen(chunk_framing)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    (void)http_response_parser_next(&p, &r);
    if (r.body_len != 0) {
        FAIL("the coded bytes were handed back as the body");
        return;
    }
    PASS();
}

/* 4. One unknown coding is enough, wherever it sits in the list: the
 *    codings are applied in turn, so an unapplied one poisons the body
 *    wherever it appears. "chunked, gzip" is refused too. */
static void test_one_unknown_coding_is_enough(void)
{
    static const char head[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked, gzip\r\n"
        "\r\n";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("one unknown coding is enough, wherever it sits in the list");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, head, strlen(head)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    errno = 0;
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("the response was accepted");
        return;
    }
    if (errno != ENOTSUP) {
        FAIL("the reason is not ENOTSUP");
        return;
    }
    PASS();
}

/* 5. A response whose only coding is one we cannot apply is refused as
 *    well: the framing may be determinate (the close of the
 *    connection), but the body is still coded. */
static void test_a_response_with_only_an_unknown_coding_is_refused(void)
{
    static const char head[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: gzip\r\n"
        "\r\n";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("a response with only an unknown coding is refused");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, head, strlen(head)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    errno = 0;
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("the response was accepted");
        return;
    }
    if (errno != ENOTSUP) {
        FAIL("the reason is not ENOTSUP");
        return;
    }
    PASS();
}

/* 6. The path that must keep working: a response whose only coding is
 *    chunked is still decoded, and the body is the decoded one. */
static void test_a_chunked_response_is_still_decoded(void)
{
    static const char head[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("a response whose only coding is chunked is still decoded");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, head, strlen(head)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    if (http_response_parser_feed(&p, chunk_framing, strlen(chunk_framing)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the response did not parse");
        return;
    }
    if (!r.framed_by_chunked) {
        FAIL("the response was not seen as chunked");
        return;
    }
    if (r.body_len != sizeof(expected_body) - 1 ||
        memcmp(r.body, expected_body, r.body_len) != 0) {
        FAIL("the decoded body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 7. The empty-element rule, on the client's side: RFC 9110 5.6.1
 *    says a recipient must accept a list that contains empty elements,
 *    so "chunked," names ONE coding -- chunked -- and the body is
 *    chunk framed. The old scan read the text after the last comma,
 *    found nothing, and treated the body as framed by the close of the
 *    connection: the raw chunk framing came back as the body. */
static void test_an_empty_list_element_does_not_hide_the_final_coding(void)
{
    static const char head[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked,\r\n"
        "\r\n";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("an empty list element does not hide the final coding");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, head, strlen(head)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    if (http_response_parser_feed(&p, chunk_framing, strlen(chunk_framing)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the response did not parse -- the empty element hid the coding");
        return;
    }
    if (!r.framed_by_chunked) {
        FAIL("the response was not seen as chunked");
        return;
    }
    if (r.body_len != sizeof(expected_body) - 1 ||
        memcmp(r.body, expected_body, r.body_len) != 0) {
        FAIL("the decoded body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 8. A coding's name is case-insensitive (RFC 9110 5.6.1). */
static void test_the_codings_name_is_case_insensitive(void)
{
    static const char head[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: CHUNKED\r\n"
        "\r\n";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("the coding's name is case-insensitive");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, head, strlen(head)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    if (http_response_parser_feed(&p, chunk_framing, strlen(chunk_framing)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the response did not parse");
        return;
    }
    if (!r.framed_by_chunked) {
        FAIL("the response was not seen as chunked");
        return;
    }
    if (r.body_len != sizeof(expected_body) - 1 ||
        memcmp(r.body, expected_body, r.body_len) != 0) {
        FAIL("the decoded body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 9. A response with no coding at all is unchanged: the length frames
 *    the body, and the body is the body. */
static void test_a_response_with_no_coding_is_unchanged(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("a response with no coding is unchanged");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, response, strlen(response)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the response did not parse");
        return;
    }
    if (!r.framed_by_length) {
        FAIL("the body was not framed by the length");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 10. The other side of the same contract: the client never SENDS a
 *     coding it cannot apply. RFC 9112 6.1 -- "A client MUST NOT send
 *     a request containing Transfer-Encoding unless it knows the server
 *     will handle HTTP/1.1 requests" -- and the only coding we send is
 *     the one we can read. */
static void test_the_client_never_sends_a_coding_it_cannot_apply(void)
{
    char req[512];
    int n;
    const char *te;

    TEST("the client never sends a coding it cannot apply");
    n = http_client_request_build_chunked_head(req, sizeof(req), "POST", "/",
                                               "example.com", 1);
    if (n <= 0) {
        FAIL("the chunked head was not built");
        return;
    }
    te = strstr(req, "Transfer-Encoding:");
    if (te == NULL) {
        FAIL("the chunked head names no coding");
        return;
    }
    if (strncmp(te, "Transfer-Encoding: chunked\r\n", 27) != 0) {
        FAIL("the head names a coding other than chunked");
        return;
    }
    if (strstr(te + 27, "Transfer-Encoding:") != NULL) {
        FAIL("the head names more than one coding");
        return;
    }
    PASS();
}

/* A peer that answers with a head and then holds the connection open:
 * the client must fail from the head alone, without waiting for a body
 * that never comes. */
static uint16_t spawn_coded_peer(pid_t *pid_out, const char *head)
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
            /* Read the request first: a peer that hangs up with an
             * unread request sends an RST, and the RST discards the
             * bytes the peer already sent. */
            ssize_t n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, head, strlen(head));
            sleep(5);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

/* 11. The whole thing over a real socket: the client's read must fail
 *     with ENOTSUP, and it must fail before the peer's deadline -- if
 *     it waited for the body, the read would time out with EAGAIN
 *     instead, and the test would say so. */
static void test_the_client_refuses_a_coded_response_over_a_socket(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    struct timeval tv;
    int rc;

    TEST("the client refuses a coded response over a real socket");
    port = spawn_coded_peer(&pid, head_gzip_chunked);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    if (setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        FAIL("the deadline could not be set");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (http_client_connection_write_request(&c, "GET", "/", NULL, 0) != 0) {
        FAIL("the request was not written");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    errno = 0;
    rc = http_client_connection_read_response(&c, &r);
    if (rc == 0) {
        FAIL("the client accepted a response it cannot decode");
        http_client_connection_close(&c);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno != ENOTSUP) {
        FAIL("the failure is not the refusal -- the client waited for the body");
        http_client_connection_close(&c);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);
    alarm(60);

    test_a_coding_we_cannot_apply_is_refused();
    test_the_refusal_happens_in_the_head();
    test_the_refusal_hands_back_no_body();
    test_one_unknown_coding_is_enough();
    test_a_response_with_only_an_unknown_coding_is_refused();
    test_a_chunked_response_is_still_decoded();
    test_an_empty_list_element_does_not_hide_the_final_coding();
    test_the_codings_name_is_case_insensitive();
    test_a_response_with_no_coding_is_unchanged();
    test_the_client_never_sends_a_coding_it_cannot_apply();
    test_the_client_refuses_a_coded_response_over_a_socket();

    printf("\n%zu/%zu tests passed\n", (size_t)tests_passed, (size_t)tests_run);
    return tests_failed == 0 ? 0 : 1;
}
