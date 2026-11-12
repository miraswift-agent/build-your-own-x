/*
 * Stage 65.26 -- the duplicate Content-Length header lines.
 *
 * Stage 65.25 read ONE Content-Length field value and applied the
 * list exception to it: "5, 5" names one value, 5. But a message can
 * carry the field MORE THAN ONCE, and then there is no single field
 * value to read -- there is a COMBINED one.
 *
 * RFC 9110 5.2 says how a repeated field name becomes one value:
 *
 *   "When a field name is repeated within a section, its combined
 *    field value consists of the list of corresponding field line
 *    values within that section, concatenated in order, with each
 *    field line value separated by a comma."
 *
 * So two field lines
 *
 *   Content-Length: 5
 *   Content-Length: 5
 *
 * have the combined field value "5, 5" -- which is exactly the list
 * Stage 65.25 already accepts as one value. And two field lines
 *
 *   Content-Length: 5
 *   Content-Length: 6
 *
 * have the combined field value "5, 6" -- a list whose values DIFFER,
 * which is an invalid framing (RFC 9112 6.3 item 5), so the server
 * answers 400 and closes, and the user agent closes the connection
 * and discards the response.
 *
 * Today both parsers take the FIRST field line and ignore the rest.
 * That is a wrong accept: a request carrying "5" and "6" is framed by
 * the first, so this server reads five bytes of body and treats the
 * rest as the beginning of the next message -- and a peer that framed
 * the message with the other value reads it differently. That is the
 * request-smuggling shape, and it is the reason RFC 9110 8.6 says a
 * sender "MUST NOT forward a message with a Content-Length header
 * field value that is known to be incorrect".
 *
 * The oracle is not us: the rules are RFC 9110 5.2 and RFC 9112 6.3
 * item 5, and the bytes below are captured wire traffic.
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
#include <strings.h>
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

/* A request carrying the given Content-Length field lines (each line
 * already ending in CRLF) and a five-byte body. The body is "hello",
 * so a length of 5 is the one that fits. */
static void build_request(char *dst, size_t dst_len, const char *cl_lines,
                          const char *connection)
{
    snprintf(dst, dst_len,
             "POST /echo HTTP/1.1\r\n"
             "Host: test\r\n"
             "%s"
             "Connection: %s\r\n"
             "\r\n"
             "hello",
             cl_lines, connection);
}

/* Send a request with the given Content-Length field lines and return
 * the status the server answered with (-1 when the exchange failed). */
static int ask_with_lines(const char *cl_lines, const char *connection,
                           http_response *r)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    char req[512];
    int status;

    build_request(req, sizeof(req), cl_lines, connection);
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

/* 1. TWO EQUAL FIELD LINES ARE ONE VALUE. The combined field value is
 *    "5, 5" (RFC 9110 5.2), and Stage 65.25's list exception says a
 *    list of equal values names one value -- so the message is
 *    processed with 5.
 *
 *    This is the case the RFCs call out: "a recipient of a
 *    Content-Length header field value consisting of the same decimal
 *    value repeated as a comma-separated list (e.g, 'Content-Length:
 *    42, 42') MAY either reject the message as invalid or replace
 *    that invalid field value with a single instance of the decimal
 *    value, since this likely indicates that a duplicate was generated
 *    or combined by an upstream message processor" (RFC 9110 8.6).
 *    The framing section says the message "is processed with that
 *    single value" (RFC 9112 6.3 item 5), so that is what we do. */
static void test_two_equal_field_lines_are_one_value(void)
{
    http_response r;

    TEST("two equal Content-Length field lines are one value");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 5\r\nContent-Length: 5\r\n",
                       "close", &r) != 200) {
        FAIL("the request was refused -- equal values name one value");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 2. TWO FIELD LINES THAT DIFFER ARE INVALID. The combined field
 *    value is "5, 6": a list whose values differ, which is an invalid
 *    framing (RFC 9112 6.3 item 5). The server answers 400 and closes
 *    the connection -- and the close is visible in the answer, so the
 *    test does not depend on the request's own Connection header. */
static void test_two_field_lines_that_differ_are_invalid(void)
{
    http_response r;
    const char *conn;

    TEST("two Content-Length field lines that differ are invalid");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 5\r\nContent-Length: 6\r\n",
                       "keep-alive", &r) != 400) {
        FAIL("the request was answered -- the values differ");
        return;
    }
    conn = http_response_header_get(&r, "Connection");
    if (!conn || strcasecmp(conn, "close") != 0) {
        FAIL("the refusal did not close the connection");
        return;
    }
    PASS();
}

/* 3. The smuggling shape: the SECOND field line names a longer body.
 *    A parser that takes the first line reads five bytes and treats
 *    the rest as the next message; a parser that takes the second
 *    waits for bytes that never come. Only the combined value says
 *    which is right, and it says neither: the framing is invalid. */
static void test_a_duplicate_naming_a_longer_body_is_invalid(void)
{
    http_response r;

    TEST("a duplicate naming a longer body is invalid");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 5\r\nContent-Length: 10\r\n",
                       "close", &r) != 400) {
        FAIL("the request was answered");
        return;
    }
    PASS();
}

/* 4. One valid field line and one invalid one: the list has a value
 *    that is not a length, so the framing is invalid ("all values in
 *    the list are valid" is part of the exception). */
static void test_a_valid_duplicate_and_an_invalid_one_are_invalid(void)
{
    http_response r;

    TEST("a valid duplicate and an invalid one are invalid");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 5\r\nContent-Length: abc\r\n",
                       "close", &r) != 400) {
        FAIL("the request was answered");
        return;
    }
    PASS();
}

/* 5. The field NAME is case-insensitive (RFC 9110 5.1), so a second
 *    line spelled differently is still the same field -- and still
 *    has to be read. */
static void test_the_field_name_is_case_insensitive(void)
{
    http_response r;

    TEST("the field name is case-insensitive");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 5\r\ncontent-length: 6\r\n",
                       "close", &r) != 400) {
        FAIL("the request was answered -- same field, different spelling");
        return;
    }
    PASS();
}

/* 6. "the same decimal value" is a number, not a spelling: 05 and 5
 *    are the same length, so the combined value "05, 5" names one
 *    value and the message is processed with it. */
static void test_the_same_decimal_value_written_differently_is_one_value(void)
{
    http_response r;

    TEST("the same decimal value written differently is one value");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 05\r\nContent-Length: 5\r\n",
                       "close", &r) != 200) {
        FAIL("the request was refused -- 05 and 5 are the same length");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 7. A field line that already carries a list, plus a second field
 *    line: the combined value is "5, 5, 5" -- one list, one value. */
static void test_a_list_field_line_and_a_duplicate_are_one_list(void)
{
    http_response r;

    TEST("a list field line and a duplicate are one list");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 5, 5\r\nContent-Length: 5\r\n",
                       "close", &r) != 200) {
        FAIL("the request was refused");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 8. The path that must keep working: ONE field line is processed, and
 *    the body that arrives is the body that is read. */
static void test_one_field_line_is_still_processed(void)
{
    http_response r;

    TEST("one Content-Length field line is still processed");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 5\r\n", "close", &r) != 200) {
        FAIL("the request was refused");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the body is not the one the peer wrote");
        return;
    }
    PASS();
}

/* 9. The path that must keep working: a request that carries a
 *    Content-Length AND a Transfer-Encoding is still refused -- the
 *    conflict rule (Stage 65.21) is checked on the presence of the
 *    two headers, and a duplicate does not change that. */
static void test_a_duplicate_with_a_transfer_encoding_is_still_refused(void)
{
    http_response r;

    TEST("a duplicate with a Transfer-Encoding is still refused");
    memset(&r, 0, sizeof(r));
    if (ask_with_lines("Content-Length: 5\r\nContent-Length: 5\r\n"
                       "Transfer-Encoding: chunked\r\n",
                       "close", &r) != 400) {
        FAIL("the request was answered -- the framing is ambiguous");
        return;
    }
    PASS();
}

/* 10. The same combined-value rule on the CLIENT's side: a response
 *     carrying two equal field lines names one value, and the client
 *     reads the body it framed. */
static void test_the_client_reads_two_equal_field_lines(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("the client reads two equal Content-Length field lines");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, response, strlen(response)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the response did not parse -- equal values name one value");
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

/* 11. A response whose two field lines DIFFER is refused, and the
 *     reason says which failure it was: the framing is invalid. */
static void test_the_client_refuses_two_field_lines_that_differ(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Content-Length: 6\r\n"
        "\r\n"
        "hello";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("the client refuses two Content-Length field lines that differ");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, response, strlen(response)) != 0) {
        FAIL("the feed was refused");
        return;
    }
    errno = 0;
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("the response was accepted -- the values differ");
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

/* 12. THE USER AGENT HALF, on the duplicate. "If it is in a response
 *     message received by a user agent, the user agent MUST close the
 *     connection to the server and discard the received response."
 *     The close is the claim, and it is observable from the other side
 *     of the wire: the peer sees EOF. */
static void test_the_client_closes_the_connection_on_differing_duplicates(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Content-Length: 6\r\n"
        "\r\n"
        "hello";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;

    TEST("the client closes the connection on differing duplicates");
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

/* 13. The path that must keep working: a response with ONE field line
 *     is read, and the body is the body the peer wrote. */
static void test_one_field_line_is_still_read(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("one Content-Length field line is still read");
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

    test_two_equal_field_lines_are_one_value();
    test_two_field_lines_that_differ_are_invalid();
    test_a_duplicate_naming_a_longer_body_is_invalid();
    test_a_valid_duplicate_and_an_invalid_one_are_invalid();
    test_the_field_name_is_case_insensitive();
    test_the_same_decimal_value_written_differently_is_one_value();
    test_a_list_field_line_and_a_duplicate_are_one_list();
    test_one_field_line_is_still_processed();
    test_a_duplicate_with_a_transfer_encoding_is_still_refused();
    test_the_client_reads_two_equal_field_lines();
    test_the_client_refuses_two_field_lines_that_differ();
    test_the_client_closes_the_connection_on_differing_duplicates();
    test_one_field_line_is_still_read();

    printf("\n%zu/%zu tests passed\n", (size_t)tests_passed, (size_t)tests_run);
    return tests_failed == 0 ? 0 : 1;
}
