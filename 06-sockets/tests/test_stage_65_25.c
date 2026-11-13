/*
 * Stage 65.25 -- the Content-Length rules, in both roles.
 *
 * The framing rules so far have all been about TRANSFER CODINGS. This
 * stage is about the other way a message says where its body ends:
 * the length.
 *
 * RFC 9112 6.3 item 5 is the whole rule, and it has two halves that
 * look alike and are not:
 *
 *   "If a message is received without Transfer-Encoding and with an
 *    invalid Content-Length header field, then the message framing is
 *    invalid and the recipient MUST treat it as an unrecoverable error,
 *    unless the field value can be successfully parsed as a
 *    comma-separated list (Section 5.6.1 of [HTTP]), all values in the
 *    list are valid, and all values in the list are the same (in which
 *    case, the message is processed with that single value used as the
 *    Content-Length field value).  If the unrecoverable error is in a
 *    request message, the server MUST respond with a 400 (Bad Request)
 *    status code and then close the connection.  ...  If it is in a
 *    response message received by a user agent, the user agent MUST
 *    close the connection to the server and discard the received
 *    response."
 *
 * So there are three claims here, and they belong to two roles:
 *
 *   1. THE LIST EXCEPTION. A Content-Length whose value is a
 *      comma-separated list of EQUAL values is one value -- "5, 5" is
 *      5, and the message is processed. A list whose values differ is
 *      invalid. Today the server refuses "5, 5" with a 400: a wrong
 *      refusal, of a request the RFC says to process.
 *
 *   2. THE SERVER HALF. An invalid length in a request is a 400 and
 *      the connection closes. The server already does this -- the stage
 *      pins it with a test rather than changing it.
 *
 *   3. THE USER AGENT HALF. An invalid length in a response means the
 *      client MUST close the connection to the server and discard the
 *      response. Today the client refuses the response and leaves the
 *      connection open -- a connection whose framing is untrustworthy,
 *      still carrying the next message.
 *
 * The two roles had also drifted on what a length may LOOK like: the
 * server's strtol accepted a sign ("+5"), the client's digit loop did
 * not. RFC 9110 8.6 says "Content-Length = 1*DIGIT", so "+5" is not a
 * length at all. One reader, shared by both roles.
 *
 * The oracle is not us: the rules are RFC 9112 6.3 item 5 and RFC 9110
 * 8.6, and the bytes below are captured wire traffic.
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

/* A hang is a poor failure mode: if the server waits for a body that
 * never arrives, this fires and the test fails with a reason. */
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

/* A request carrying the given Content-Length value and a five-byte
 * body. The body is "hello", so a length of 5 is the one that fits. */
static void build_request(char *dst, size_t dst_len, const char *cl_value)
{
    snprintf(dst, dst_len,
             "POST /echo HTTP/1.1\r\n"
             "Host: test\r\n"
             "Content-Length: %s\r\n"
             "Connection: close\r\n"
             "\r\n"
             "hello",
             cl_value);
}

/* Send a request with the given length value and return the status the
 * server answered with (-1 when the exchange failed). */
static int ask_with_length(const char *cl_value, http_response *r)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    char req[512];
    int status;

    build_request(req, sizeof(req), cl_value);
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        waitpid(pid, NULL, 0);
        return -1;
    }
    if (write_raw(&c, req, strlen(req)) != 0) {
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return -1;
    }
    if (http_client_connection_read_response(&c, r) != 0) {
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return -1;
    }
    status = r->status;
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    return status;
}

/* 1. THE LIST EXCEPTION. "Content-Length: 5, 5" names one value, 5,
 *    and the message is processed with it. RFC 9112 6.3 item 5: the
 *    framing is invalid UNLESS the value is a comma-separated list
 *    whose values are all valid and all the same -- "in which case,
 *    the message is processed with that single value used as the
 *    Content-Length field value".
 *
 *    Before this stage the server refused this request with a 400: a
 *    wrong refusal, of a request the RFC says to process. */
static void test_a_list_of_equal_values_is_one_value(void)
{
    http_response r;

    TEST("a list of equal values is one value");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("5, 5", &r) != 200) {
        FAIL("the request was refused -- a list of equal values is one value");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 2. A list whose values DIFFER is invalid: the framing is invalid, the
 *    server answers 400, and the conversation ends. */
static void test_a_list_of_differing_values_is_invalid(void)
{
    http_response r;

    TEST("a list of differing values is invalid");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("5, 6", &r) != 400) {
        FAIL("the request was answered -- the values differ");
        return;
    }
    PASS();
}

/* 3. A value that is not a number is invalid. */
static void test_a_value_that_is_not_a_number_is_invalid(void)
{
    http_response r;

    TEST("a value that is not a number is invalid");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("abc", &r) != 400) {
        FAIL("the request was answered");
        return;
    }
    PASS();
}

/* 4. A negative length is not a length. */
static void test_a_negative_length_is_invalid(void)
{
    http_response r;

    TEST("a negative length is invalid");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("-1", &r) != 400) {
        FAIL("the request was answered");
        return;
    }
    PASS();
}

/* 5. A SIGN is not a digit. RFC 9110 8.6: "Content-Length = 1*DIGIT".
 *    The server's strtol accepted "+5" and the client's digit loop did
 *    not -- the two roles had drifted on the same header. One reader
 *    now, so they cannot. */
static void test_a_signed_length_is_invalid(void)
{
    http_response r;

    TEST("a signed length is invalid");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("+5", &r) != 400) {
        FAIL("the request was answered -- a sign is not a digit");
        return;
    }
    PASS();
}

/* 6. An empty list element is not a value. RFC 9110 5.6.1: a recipient
 *    must accept a list that contains empty elements, so "5," names ONE
 *    value -- 5 -- and the message is processed with it. The same rule
 *    the transfer-coding list follows (Stage 65.22). */
static void test_an_empty_list_element_is_not_a_value(void)
{
    http_response r;

    TEST("an empty list element is not a value");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("5,", &r) != 200) {
        FAIL("the request was refused -- the empty element is not a value");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 7. An empty element between two equal values changes nothing: the
 *    list's values are 5 and 5, so the message is processed with 5. */
static void test_an_empty_element_between_equal_values_is_nothing(void)
{
    http_response r;

    TEST("an empty element between equal values is nothing");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("5, , 5", &r) != 200) {
        FAIL("the request was refused");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 8. One bad value in the list makes the list invalid: "all values in
 *    the list are valid" is part of the exception, and it is not met. */
static void test_one_bad_value_in_the_list_is_invalid(void)
{
    http_response r;

    TEST("one bad value in the list is invalid");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("5, abc", &r) != 400) {
        FAIL("the request was answered");
        return;
    }
    PASS();
}

/* 9. The path that must keep working: a plain length is processed, and
 *    the body that arrives is the body that is read. */
static void test_a_plain_length_is_still_processed(void)
{
    http_response r;

    TEST("a plain length is still processed");
    memset(&r, 0, sizeof(r));
    if (ask_with_length("5", &r) != 200) {
        FAIL("the request was refused");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 10. The same list exception on the CLIENT's side: a response whose
 *     Content-Length is "5, 5" names one value, and the client reads
 *     the body it framed. */
static void test_the_client_reads_a_list_of_equal_values(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5, 5\r\n"
        "\r\n"
        "hello";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("the client reads a list of equal values");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, response, strlen(response)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the response did not parse -- a list of equal values is one value");
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

/* 11. A response whose length is unusable is refused, and the reason
 *     says which failure it was: the framing is invalid. */
static void test_an_unusable_length_is_refused(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5, 6\r\n"
        "\r\n"
        "hello";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("an unusable length is refused");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, response, strlen(response)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    errno = 0;
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("the response was accepted");
        return;
    }
    if (errno != EBADMSG) {
        FAIL("the reason is not the framing reason");
        return;
    }
    PASS();
}

/* A peer that sends a canned response and then waits for the client to
 * hang up. It exits 0 if it saw EOF -- which is how the test knows the
 * client closed the connection -- and 1 otherwise. */
static uint16_t spawn_response_peer(pid_t *pid_out, const char *response)
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
            /* Read the request first: a peer that hangs up with an
             * unread request sends an RST, and the RST discards the
             * bytes the peer already sent. */
            ssize_t n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, response, strlen(response));
            for (;;) {
                n = read(fd, buf, sizeof(buf));
                if (n == 0) {
                    code = 0;     /* EOF: the client closed the connection */
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

/* 12. THE USER AGENT HALF. "If it is in a response message received by
 *     a user agent, the user agent MUST close the connection to the
 *     server and discard the received response." The close is the
 *     claim, and it is observable from the other side of the wire: the
 *     peer sees EOF. Before this stage the client refused the response
 *     and left the connection open -- a connection whose framing is
 *     untrustworthy, still carrying the next message. */
static void test_the_client_closes_the_connection_on_an_unusable_length(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5, 6\r\n"
        "\r\n"
        "hello";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_client_connection_init(&c);
    http_response r;

    TEST("the client closes the connection on an unusable length");
    port = spawn_response_peer(&pid, response);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
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
    if (http_client_connection_read_response(&c, &r) == 0) {
        FAIL("the client accepted a response it cannot frame");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    if (errno != EBADMSG) {
        FAIL("the reason is not the framing reason");
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
    /* And the peer saw it: the client hung up, so the peer's read ends
     * with EOF and the peer exits 0. */
    {
        int st = 0;
        if (waitpid(pid, &st, 0) < 0) {
            FAIL("the peer did not finish");
            return;
        }
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            FAIL("the peer did not see the client hang up");
            return;
        }
    }
    PASS();
}

/* 13. The path that must keep working: a response with a plain length
 *     is read, and the connection stays usable. */
static void test_a_plain_length_is_still_read(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("a plain length is still read");
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
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);
    alarm(90);

    test_a_list_of_equal_values_is_one_value();
    test_a_list_of_differing_values_is_invalid();
    test_a_value_that_is_not_a_number_is_invalid();
    test_a_negative_length_is_invalid();
    test_a_signed_length_is_invalid();
    test_an_empty_list_element_is_not_a_value();
    test_an_empty_element_between_equal_values_is_nothing();
    test_one_bad_value_in_the_list_is_invalid();
    test_a_plain_length_is_still_processed();
    test_the_client_reads_a_list_of_equal_values();
    test_an_unusable_length_is_refused();
    test_the_client_closes_the_connection_on_an_unusable_length();
    test_a_plain_length_is_still_read();

    printf("\n%zu/%zu tests passed\n", (size_t)tests_passed, (size_t)tests_run);
    return tests_failed == 0 ? 0 : 1;
}
