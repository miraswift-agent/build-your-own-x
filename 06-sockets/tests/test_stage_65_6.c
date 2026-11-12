/*
 * Stage 65.6 tests: HTTP/1.1 from the wire.
 *
 * The lesson: HTTP is text with rules, and the server is a state machine
 * over bytes.
 *
 * Stage 65.5 built a binary framer -- a 4-byte length prefix. HTTP is
 * the other shape: the boundaries are written into the text as CRLF, and
 * a request is "a line, some lines, a blank line, and then however many
 * bytes the headers say". So these tests are mostly about the rules:
 * the request line, the headers, Content-Length, keep-alive, and 404.
 *
 * The two mistakes the stage is built to catch:
 *
 *   - treating "the rest has not arrived yet" as an error. It is not an
 *     error, it is a request that is not ready, and a server that treats
 *     it as an error closes connections it should have waited on.
 *   - getting keep-alive backwards. HTTP/1.1 keeps the connection open
 *     by default; HTTP/1.0 closes it by default. The failure mode is a
 *     browser that hangs, minutes later, which is why it gets its own
 *     test rather than being folded into another.
 *
 * Hermetic: loopback only, and the last three tests talk to a server
 * forked in this process over 127.0.0.1. No network, no external host.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/http.h"
#include "../src/sockets.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-52s", #name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("✓\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        printf("✗ %s\n", msg); \
    } while (0)

static const char *find_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    const char *p;
    for (p = hay; *p; p++) {
        if (strncasecmp(p, needle, n) == 0) {
            return p;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The parser                                                          */
/* ------------------------------------------------------------------ */

static void test_parser_needs_more_bytes(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET / HTT", strlen("GET / HTT"));
    TEST("parser says NEED_MORE, not error");
    if (http_parser_next(&p, &r) == HTTP_PARSE_NEED_MORE) {
        PASS();
    } else {
        FAIL("a half-arrived request is not ready, not broken");
    }
}

static void test_parses_simple_get(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n", strlen("GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n"));
    TEST("parses method, target, version");
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        FAIL("did not parse");
        return;
    }
    if (strcmp(r.method, "GET") == 0 && strcmp(r.target, "/index.html") == 0 &&
        strcmp(r.version, "HTTP/1.1") == 0) {
        PASS();
    } else {
        FAIL("wrong method/target/version");
    }
}

static void test_header_lookup_is_case_insensitive(void)
{
    http_parser p;
    http_request r;
    const char *v;

    http_parser_init(&p);
    http_parser_feed(&p, "GET / HTTP/1.1\r\ncOnTeNt-TyPe: text/html\r\n\r\n", strlen("GET / HTTP/1.1\r\ncOnTeNt-TyPe: text/html\r\n\r\n"));
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        TEST("header lookup is case-insensitive");
        FAIL("did not parse");
        return;
    }
    v = http_header_get(&r, "Content-Type");
    TEST("header lookup is case-insensitive");
    if (v && strcmp(v, "text/html") == 0) {
        PASS();
    } else {
        FAIL("header not found under a different case");
    }
}

static void test_header_value_is_trimmed(void)
{
    http_parser p;
    http_request r;
    const char *v;

    http_parser_init(&p);
    http_parser_feed(&p, "GET / HTTP/1.1\r\nX-Trace:   abc123   \r\n\r\n", strlen("GET / HTTP/1.1\r\nX-Trace:   abc123   \r\n\r\n"));
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        TEST("header value is trimmed");
        FAIL("did not parse");
        return;
    }
    v = http_header_get(&r, "X-Trace");
    TEST("header value is trimmed");
    if (v && strcmp(v, "abc123") == 0) {
        PASS();
    } else {
        FAIL("value kept its padding");
    }
}

static void test_parses_content_length_body(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello", strlen("POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello"));
    TEST("parses a Content-Length body");
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        FAIL("did not parse");
        return;
    }
    if (r.content_length == 5 && r.body_len == 5 &&
        memcmp(r.body, "hello", 5) == 0) {
        PASS();
    } else {
        FAIL("body wrong or missing");
    }
}

static void test_body_not_arrived_is_need_more(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nhe", strlen("POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nhe"));
    TEST("body not fully arrived is NEED_MORE");
    if (http_parser_next(&p, &r) == HTTP_PARSE_NEED_MORE) {
        PASS();
    } else {
        FAIL("a short body is not ready, not broken");
    }
}

static void test_rejects_malformed_request_line(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET\r\n\r\n", strlen("GET\r\n\r\n"));
    TEST("rejects a request line with no version");
    if (http_parser_next(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("accepted a broken request line");
    }
}

static void test_rejects_non_http_version(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET / SPDY/3\r\n\r\n", strlen("GET / SPDY/3\r\n\r\n"));
    TEST("rejects a non-HTTP version");
    if (http_parser_next(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("accepted SPDY/3 as HTTP");
    }
}

static void test_rejects_header_without_colon(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET / HTTP/1.1\r\nBadHeader\r\n\r\n", strlen("GET / HTTP/1.1\r\nBadHeader\r\n\r\n"));
    TEST("rejects a header with no colon");
    if (http_parser_next(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("accepted a header with no colon");
    }
}

static void test_rejects_bad_content_length(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n", strlen("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n"));
    TEST("rejects a Content-Length that is not a number");
    if (http_parser_next(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("accepted a non-numeric Content-Length");
    }
}

static void test_two_requests_in_one_feed(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n", strlen("GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n"));
    TEST("two requests in one feed");
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        FAIL("first did not parse");
        return;
    }
    if (strcmp(r.target, "/a") != 0) {
        FAIL("first request wrong");
        return;
    }
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        FAIL("second did not parse");
        return;
    }
    if (strcmp(r.target, "/b") == 0) {
        PASS();
    } else {
        FAIL("second request wrong");
    }
}

static void test_leftover_bytes_survive_a_parse(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\nEXTRA", strlen("GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\nEXTRA"));
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        TEST("leftover bytes survive a parse");
        FAIL("first did not parse");
        return;
    }
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        TEST("leftover bytes survive a parse");
        FAIL("second did not parse");
        return;
    }
    TEST("leftover bytes survive a parse");
    if (p.len == 5 && memcmp(p.buf, "EXTRA", 5) == 0) {
        PASS();
    } else {
        FAIL("the tail of the buffer was eaten");
    }
}

/* ------------------------------------------------------------------ */
/* Keep-alive                                                          */
/* ------------------------------------------------------------------ */

static void test_keep_alive_defaults_on_http11(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET / HTTP/1.1\r\n\r\n", strlen("GET / HTTP/1.1\r\n\r\n"));
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        TEST("HTTP/1.1 keeps the connection alive");
        FAIL("did not parse");
        return;
    }
    TEST("HTTP/1.1 keeps the connection alive");
    if (http_request_keep_alive(&r) == 1) {
        PASS();
    } else {
        FAIL("HTTP/1.1 should keep-alive by default");
    }
}

static void test_keep_alive_defaults_off_http10(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET / HTTP/1.0\r\n\r\n", strlen("GET / HTTP/1.0\r\n\r\n"));
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        TEST("HTTP/1.0 closes by default");
        FAIL("did not parse");
        return;
    }
    TEST("HTTP/1.0 closes by default");
    if (http_request_keep_alive(&r) == 0) {
        PASS();
    } else {
        FAIL("HTTP/1.0 should close by default");
    }
}

static void test_connection_close_overrides(void)
{
    http_parser p;
    http_request r;

    http_parser_init(&p);
    http_parser_feed(&p, "GET / HTTP/1.1\r\nConnection: close\r\n\r\n", strlen("GET / HTTP/1.1\r\nConnection: close\r\n\r\n"));
    if (http_parser_next(&p, &r) != HTTP_PARSE_OK) {
        TEST("Connection: close overrides the default");
        FAIL("did not parse");
        return;
    }
    TEST("Connection: close overrides the default");
    if (http_request_keep_alive(&r) == 0) {
        PASS();
    } else {
        FAIL("Connection: close was ignored");
    }
}

/* ------------------------------------------------------------------ */
/* The response                                                        */
/* ------------------------------------------------------------------ */

static void test_response_build_200(void)
{
    char buf[512];
    int n;

    n = http_response_build(buf, sizeof(buf), 200, "OK", "text/html",
                            "<html>hi</html>\n", 16, 1, "Sun, 06 Nov 1994 08:49:37 GMT");
    TEST("builds a 200 response");
    if (n < 0) {
        FAIL("build failed");
        return;
    }
    if (strncmp(buf, "HTTP/1.1 200 OK\r\n", 17) == 0 &&
        strstr(buf, "Content-Length: 16\r\n") != NULL &&
        strstr(buf, "Connection: keep-alive\r\n") != NULL &&
        memcmp(buf + n - 16, "<html>hi</html>\n", 16) == 0) {
        PASS();
    } else {
        FAIL("response wrong");
    }
}

static void test_response_build_404_and_close(void)
{
    char buf[512];
    int n;

    n = http_response_build(buf, sizeof(buf), 404, "Not Found", "text/plain",
                            "nope\n", 5, 0, "Sun, 06 Nov 1994 08:49:37 GMT");
    TEST("builds a 404 that closes");
    if (n < 0) {
        FAIL("build failed");
        return;
    }
    if (strncmp(buf, "HTTP/1.1 404 Not Found\r\n", 24) == 0 &&
        strstr(buf, "Connection: close\r\n") != NULL) {
        PASS();
    } else {
        FAIL("response wrong");
    }
}

static void test_response_build_refuses_to_overflow(void)
{
    char head[512];
    char buf[512];
    int n;
    int head_len;

    /* A buffer that holds the head exactly: the body does not fit.
     * (A small fixed buffer would now fail on the head, which is not
     * what this test is about -- the Date header made the head
     * longer, and a test that quietly stops testing its claim is
     * worse than a test that fails.) */
    n = http_response_build(head, sizeof(head), 200, "OK", "text/plain", "", 0, 1,
                            "Sun, 06 Nov 1994 08:49:37 GMT");
    if (n <= 0) {
        TEST("refuses to write past the buffer");
        FAIL("the head did not build");
        return;
    }
    head_len = n;
    n = http_response_build(buf, (size_t)head_len, 200, "OK", "text/plain",
                            "hello", 5, 1, "Sun, 06 Nov 1994 08:49:37 GMT");
    TEST("refuses to write past the buffer");
    if (n < 0) {
        PASS();
    } else {
        FAIL("wrote past the end of the buffer");
    }
}

/* ------------------------------------------------------------------ */
/* Over the wire                                                      */
/* ------------------------------------------------------------------ */

static uint16_t spawn_server(const char *root_body, pid_t *pid_out)
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
            http_serve_connection(fd, root_body, &st);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

static void reap(pid_t pid)
{
    int status;
    waitpid(pid, &status, 0);
}

/* Read one response: until the headers are complete and the body
 * Content-Length bytes have arrived. */
static int read_response(int fd, char *out, size_t out_len)
{
    size_t total = 0;
    size_t hdr_end = (size_t)-1;
    long want = 0;

    for (;;) {
        ssize_t n;
        if (total + 1 >= out_len) {
            break;
        }
        n = read(fd, out + total, out_len - total - 1);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;              /* EOF */
        }
        total += (size_t)n;
        out[total] = '\0';
        if (hdr_end == (size_t)-1) {
            char *p = strstr(out, "\r\n\r\n");
            if (p) {
                const char *cl;
                hdr_end = (size_t)(p - out) + 4;
                cl = find_ci(out, "Content-Length:");
                if (cl) {
                    want = strtol(cl + 15, NULL, 10);
                }
            }
        }
        if (hdr_end != (size_t)-1 && total >= hdr_end + (size_t)want) {
            break;
        }
    }
    out[total] = '\0';
    return (int)total;
}

static void test_serves_root_over_the_wire(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char resp[4096];
    int n;

    port = spawn_server("<html>hi</html>\n", &pid);
    fd = sock_connect_loopback(port);
    TEST("serves / over the wire");
    if (fd < 0) {
        FAIL("connect failed");
        reap(pid);
        return;
    }
    {
        sock_io_result wr;
        const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        sock_write_exact(fd, req, strlen(req), &wr);
    }
    n = read_response(fd, resp, sizeof(resp));
    if (n > 0 && strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) == 0 &&
        strstr(resp, "Content-Length: 16\r\n") != NULL &&
        memcmp(resp + n - 16, "<html>hi</html>\n", 16) == 0) {
        PASS();
    } else {
        FAIL("wrong response from the wire");
    }
    sock_close(fd);
    reap(pid);
}

static void test_serves_404_over_the_wire(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char resp[4096];
    int n;

    port = spawn_server("<html>hi</html>\n", &pid);
    fd = sock_connect_loopback(port);
    TEST("serves 404 for an unknown path");
    if (fd < 0) {
        FAIL("connect failed");
        reap(pid);
        return;
    }
    {
        sock_io_result wr;
        const char *req = "GET /nope HTTP/1.1\r\n\r\n";
        sock_write_exact(fd, req, strlen(req), &wr);
    }
    n = read_response(fd, resp, sizeof(resp));
    if (n > 0 && strncmp(resp, "HTTP/1.1 404 Not Found\r\n", 24) == 0 &&
        strstr(resp, "404 Not Found") != NULL) {
        PASS();
    } else {
        FAIL("no 404 from the wire");
    }
    sock_close(fd);
    reap(pid);
}

static void test_keeps_the_connection_alive(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char resp[4096];
    int n;

    port = spawn_server("<html>hi</html>\n", &pid);
    fd = sock_connect_loopback(port);
    TEST("serves two requests on one connection");
    if (fd < 0) {
        FAIL("connect failed");
        reap(pid);
        return;
    }
    {
        sock_io_result wr;
        const char *r1 = "GET / HTTP/1.1\r\n\r\n";
        sock_write_exact(fd, r1, strlen(r1), &wr);
    }
    n = read_response(fd, resp, sizeof(resp));
    if (n <= 0 || strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("first request failed");
        sock_close(fd);
        reap(pid);
        return;
    }
    {
        sock_io_result wr;
        const char *r2 = "GET /second HTTP/1.1\r\n\r\n";
        sock_write_exact(fd, r2, strlen(r2), &wr);
    }
    n = read_response(fd, resp, sizeof(resp));
    if (n > 0 && strncmp(resp, "HTTP/1.1 404 Not Found\r\n", 24) == 0) {
        PASS();
    } else {
        FAIL("the second request on the same connection failed");
    }
    sock_close(fd);
    reap(pid);
}

int main(void)
{
    printf("Stage 65.6 tests: HTTP/1.1 from the wire\n\n");

    test_parser_needs_more_bytes();
    test_parses_simple_get();
    test_header_lookup_is_case_insensitive();
    test_header_value_is_trimmed();
    test_parses_content_length_body();
    test_body_not_arrived_is_need_more();
    test_rejects_malformed_request_line();
    test_rejects_non_http_version();
    test_rejects_header_without_colon();
    test_rejects_bad_content_length();
    test_two_requests_in_one_feed();
    test_leftover_bytes_survive_a_parse();

    test_keep_alive_defaults_on_http11();
    test_keep_alive_defaults_off_http10();
    test_connection_close_overrides();

    test_response_build_200();
    test_response_build_404_and_close();
    test_response_build_refuses_to_overflow();

    test_serves_root_over_the_wire();
    test_serves_404_over_the_wire();
    test_keeps_the_connection_alive();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("  (%d FAILED)\n", tests_failed);
        return 1;
    }
    printf("\n");
    return 0;
}
