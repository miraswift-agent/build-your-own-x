/*
 * Stage 65.7 -- HTTP/1.1 client from the wire.
 *
 * The tests are about framing, because that is where the client breaks:
 * what makes a response complete, what makes it broken, and what makes it
 * merely unfinished.
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

#include "../src/http_client.h"
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


static void reap(pid_t pid)
{
    int status;
    waitpid(pid, &status, 0);
}

static void feed(http_response_parser *p, const char *s)
{
    http_response_parser_feed(p, s, strlen(s));
}

/* A server in a child process: the Stage 65.6 one, over a real socket. */
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

static void test_builds_get_request(void)
{
    char req[1024];
    const char *prefix = "GET /index.html HTTP/1.1\r\n";
    int n;

    TEST("builds a GET request");
    n = http_client_request_build(req, sizeof(req), "GET", "/index.html",
                                  "example.com", NULL, 0);
    if (n > 0 &&
        strncmp(req, prefix, strlen(prefix)) == 0 &&
        strstr(req, "Host: example.com\r\n") != NULL &&
        n >= 4 && memcmp(req + n - 4, "\r\n\r\n", 4) == 0) {
        PASS();
    } else {
        FAIL("the request is not what we asked for");
    }
}

static void test_sets_content_length_for_a_body(void)
{
    char req[1024];
    const char *body = "hello";
    int n;

    TEST("sets Content-Length for a body");
    n = http_client_request_build(req, sizeof(req), "POST", "/submit",
                                  "example.com", body, strlen(body));
    if (n > 0 &&
        strstr(req, "Content-Length: 5\r\n") != NULL &&
        memcmp(req + n - 5, body, 5) == 0) {
        PASS();
    } else {
        FAIL("the body was not framed by its length");
    }
}

static void test_refuses_a_small_buffer(void)
{
    char small[16];
    int n;

    TEST("refuses a buffer that is too small");
    errno = 0;
    n = http_client_request_build(small, sizeof(small), "GET", "/",
                                  "example.com", NULL, 0);
    if (n == -1 && errno == ENOBUFS) {
        PASS();
    } else {
        FAIL("it wrote past the end of the buffer");
    }
}

static void test_parses_a_status_line(void)
{
    http_response_parser p;
    http_response r;

    TEST("parses a status line");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi");
    if (http_response_parser_next(&p, &r) == HTTP_PARSE_OK &&
        r.status == 200 &&
        strcmp(r.reason, "OK") == 0 &&
        strcmp(r.version, "HTTP/1.1") == 0 &&
        r.body_len == 2 &&
        memcmp(r.body, "hi", 2) == 0) {
        PASS();
    } else {
        FAIL("the response did not parse");
    }
}

static void test_parses_a_response_with_no_reason(void)
{
    http_response_parser p;
    http_response r;

    TEST("parses a response with no reason phrase");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 204\r\n\r\n");
    if (http_response_parser_finish(&p, &r) == HTTP_PARSE_OK &&
        r.status == 204 &&
        r.reason[0] == '\0' &&
        r.body_len == 0) {
        PASS();
    } else {
        FAIL("a reasonless status line did not parse");
    }
}

static void test_rejects_a_non_http_status_line(void)
{
    http_response_parser p;
    http_response r;

    TEST("rejects a status line that is not HTTP");
    http_response_parser_init(&p);
    feed(&p, "ICY 200 OK\r\n\r\n");
    if (http_response_parser_next(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("it accepted a status line that is not HTTP");
    }
}

static void test_rejects_a_two_digit_code(void)
{
    http_response_parser p;
    http_response r;

    TEST("rejects a status code that is not three digits");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 20 OK\r\n\r\n");
    if (http_response_parser_next(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("it accepted a two-digit status code");
    }
}

static void test_header_lookup_is_case_insensitive(void)
{
    http_response_parser p;
    http_response r;
    const char *v;

    TEST("response header lookup is case-insensitive");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 200 OK\r\nX-Thing: yes\r\nContent-Length: 0\r\n\r\n");
    if (http_response_parser_next(&p, &r) == HTTP_PARSE_OK) {
        v = http_response_header_get(&r, "x-thing");
        if (v != NULL && strcmp(v, "yes") == 0) {
            PASS();
        } else {
            FAIL("the header was not found");
        }
    } else {
        FAIL("the response did not parse");
    }
}

static void test_body_not_arrived_is_need_more(void)
{
    http_response_parser p;
    http_response r;

    TEST("a body that has not all arrived is NEED_MORE");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhi");
    if (http_response_parser_next(&p, &r) == HTTP_PARSE_NEED_MORE) {
        PASS();
    } else {
        FAIL("a half-arrived body was not NEED_MORE");
    }
}

static void test_no_content_length_ends_at_eof(void)
{
    http_response_parser p;
    http_response r;

    TEST("no Content-Length means the body ends at EOF");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 200 OK\r\n\r\nhello");
    if (http_response_parser_next(&p, &r) == HTTP_PARSE_NEED_MORE &&
        http_response_parser_finish(&p, &r) == HTTP_PARSE_OK &&
        r.body_len == 5 &&
        memcmp(r.body, "hello", 5) == 0 &&
        r.framed_by_length == 0) {
        PASS();
    } else {
        FAIL("an EOF-framed body did not complete at the close");
    }
}

static void test_truncated_response_is_an_error(void)
{
    http_response_parser p;
    http_response r;

    TEST("a Content-Length response that ends early is an error");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhi");
    if (http_response_parser_finish(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("a truncated response was accepted as a short one");
    }
}

static void test_parses_a_response_with_no_headers(void)
{
    http_response_parser p;
    http_response r;

    TEST("parses a response with no headers at all");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 204 No Content\r\n\r\n");
    if (http_response_parser_finish(&p, &r) == HTTP_PARSE_OK &&
        r.nheaders == 0 &&
        r.status == 204) {
        PASS();
    } else {
        FAIL("a headerless response did not parse");
    }
}

static void test_rejects_a_header_without_a_colon(void)
{
    http_response_parser p;
    http_response r;

    TEST("rejects a header without a colon");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 200 OK\r\nBadHeader\r\n\r\n");
    if (http_response_parser_next(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("it accepted a header with no colon");
    }
}

static void test_rejects_a_bad_content_length(void)
{
    http_response_parser p;
    http_response r;

    TEST("rejects a Content-Length that is not a number");
    http_response_parser_init(&p);
    feed(&p, "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n");
    if (http_response_parser_next(&p, &r) == HTTP_PARSE_ERROR) {
        PASS();
    } else {
        FAIL("it accepted a Content-Length that is not a number");
    }
}

static void test_gets_over_a_socket(void)
{
    pid_t pid;
    uint16_t port;
    http_response r;
    const char *body = "<html>hi</html>\n";

    port = spawn_server(body, &pid);
    TEST("GETs over a real socket and gets the body back");
    if (http_client_get("127.0.0.1", port, "/", &r) == 0 &&
        r.status == 200 &&
        r.body_len == strlen(body) &&
        memcmp(r.body, body, r.body_len) == 0) {
        PASS();
    } else {
        FAIL("the round trip did not come back with the body");
    }
    reap(pid);
}

static void test_gets_a_missing_path(void)
{
    pid_t pid;
    uint16_t port;
    http_response r;

    port = spawn_server("<html>hi</html>\n", &pid);
    TEST("GETs a path that does not exist and gets a 404");
    if (http_client_get("127.0.0.1", port, "/missing", &r) == 0 &&
        r.status == 404) {
        PASS();
    } else {
        FAIL("the 404 did not come back");
    }
    reap(pid);
}

int main(void)
{
    test_builds_get_request();
    test_sets_content_length_for_a_body();
    test_refuses_a_small_buffer();
    test_parses_a_status_line();
    test_parses_a_response_with_no_reason();
    test_rejects_a_non_http_status_line();
    test_rejects_a_two_digit_code();
    test_header_lookup_is_case_insensitive();
    test_body_not_arrived_is_need_more();
    test_no_content_length_ends_at_eof();
    test_truncated_response_is_an_error();
    test_parses_a_response_with_no_headers();
    test_rejects_a_header_without_a_colon();
    test_rejects_a_bad_content_length();
    test_gets_over_a_socket();
    test_gets_a_missing_path();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("  (%d FAILED)\n", tests_failed);
        return 1;
    }
    printf("\n");
    return 0;
}
