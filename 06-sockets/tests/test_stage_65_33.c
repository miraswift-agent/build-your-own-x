/*
 * Stage 65.33 -- the 100 Continue.
 *
 * RFC 9110 10.1.1: a client may send "Expect: 100-continue" to ask
 * the server to confirm that it will take the body before the body is
 * sent. A server that receives the expectation MUST send either a 100
 * (Continue) interim response or a final response.
 *
 * Until now our server did the second half only -- it waited for the
 * whole request and then answered -- so a client waiting for a 100
 * before it sent the body waited for nothing. That is not a
 * hypothetical: curl sends "Expect: 100-continue" by itself for a
 * body over 1024 bytes, and waits about a second before sending the
 * body anyway.
 *
 * The stage has three parts:
 *
 *   - the parser can say "the head is complete" on its own, so the
 *     server can answer before the body has arrived;
 *   - the builder can make an interim response: no Date (RFC 9110
 *     6.6.1), no Content-Length (RFC 9110 8.6), no content;
 *   - the serving loop sends the 100 when -- and only when -- the
 *     client asked for it and the body has not all arrived.
 *
 * The oracle is the bytes on the wire: an interim response is a
 * status line and nothing else.
 */
#include "../src/http.h"
#include "../src/sockets.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
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

/* Read a header block: one byte at a time, so nothing past the empty
 * line is swallowed. */
static ssize_t read_head(int fd, char *buf, size_t cap)
{
    size_t got = 0;

    while (got + 1 < cap) {
        ssize_t n = read(fd, buf + got, 1);
        if (n <= 0) {
            return -1;
        }
        got++;
        if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0) {
            buf[got] = '\0';
            return (ssize_t)got;
        }
    }
    return -1;
}

/* The Content-Length a head claims, or 0. */
static long content_length_of(const char *head)
{
    const char *p = strstr(head, "\nContent-Length:");
    char *end;
    long v;

    if (p == NULL) {
        if (strncmp(head, "Content-Length:", 16) != 0) {
            return 0;
        }
        p = head;
    } else {
        p++;
    }
    p += 16;
    v = strtol(p, &end, 10);
    if (end == p) {
        return 0;
    }
    return v;
}

/* Is there anything to read within ms? */
static int readable_in(int fd, int ms)
{
    fd_set r;
    struct timeval tv;

    FD_ZERO(&r);
    FD_SET(fd, &r);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select(fd + 1, &r, NULL, NULL, &tv) > 0;
}

static const char expect_head[] =
    "POST /echo HTTP/1.1\r\n"
    "Host: test\r\n"
    "Expect: 100-continue\r\n"
    "Content-Length: 5\r\n"
    "\r\n";

/* 1. THE PARSER. The head is complete before the body is -- and the
 *    parser has to be able to SAY so, because that is what lets the
 *    server answer in between. */
static void test_the_parser_says_the_head_is_complete_before_the_body(void)
{
    http_parser p;
    http_request req;
    static const char head[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 5\r\n"
        "\r\n";

    TEST("the parser says the head is complete before the body");
    http_parser_init(&p);
    if (http_parser_feed(&p, head, sizeof(head) - 1) != 0) {
        FAIL("the feed failed");
        return;
    }
    if (http_parser_head_next(&p, &req) != HTTP_PARSE_OK) {
        FAIL("the head was not reported complete");
        return;
    }
    if (req.content_length != 5) {
        FAIL("the length was not read");
        return;
    }
    if (!http_parser_body_pending(&p, &req)) {
        FAIL("the body was not reported pending");
        return;
    }
    if (http_parser_next(&p, &req) != HTTP_PARSE_NEED_MORE) {
        FAIL("the whole request was reported ready");
        return;
    }
    if (http_parser_feed(&p, "hello", 5) != 0) {
        FAIL("the feed failed");
        return;
    }
    if (http_parser_next(&p, &req) != HTTP_PARSE_OK) {
        FAIL("the request was not ready after the body");
        return;
    }
    if (req.body_len != 5 || memcmp(req.body, "hello", 5) != 0) {
        FAIL("the body was wrong");
        return;
    }
    PASS();
}

/* 2. THE SERVER. The 100 goes out before the body arrives, and it is
 *    a bare interim response: no Date (RFC 9110 6.6.1), no
 *    Content-Length (RFC 9110 8.6), no content. */
static void test_the_server_answers_a_100_before_the_body(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char interim[256];
    ssize_t n;
    char head[1024];
    char body[1024];
    long clen;

    TEST("the server answers a 100 before the body");
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connect failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (sock_write_once(fd, expect_head, sizeof(expect_head) - 1) !=
        (ssize_t)(sizeof(expect_head) - 1)) {
        FAIL("the head was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    n = read_head(fd, interim, sizeof(interim));
    if (n < 0) {
        FAIL("the interim response did not arrive");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strcmp(interim, "HTTP/1.1 100 Continue\r\n\r\n") != 0) {
        FAIL("the interim response was not a bare 100 Continue");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(interim, "Date:") != NULL) {
        FAIL("the 100 carried a Date");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(interim, "Content-Length:") != NULL) {
        FAIL("the 100 carried a Content-Length");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    /* The body, and then the final response. */
    if (sock_write_once(fd, "hello", 5) != 5) {
        FAIL("the body was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    n = read_head(fd, head, sizeof(head));
    if (n < 0 || strncmp(head, "HTTP/1.1 200 OK", 15) != 0) {
        FAIL("the final response was not a 200");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    clen = content_length_of(head);
    if (clen != 5) {
        FAIL("the final response did not claim the body");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (read(fd, body, 5) != 5 || memcmp(body, "hello", 5) != 0) {
        FAIL("the body was not echoed");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    sock_close(fd);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 3. AN EXPECTATION WITH NO BODY. There is nothing to wait for, so
 *    there is nothing to confirm: the final response alone satisfies
 *    the MUST (RFC 9110 10.1.1). */
static void test_an_expectation_with_no_body_gets_no_100(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char head[1024];
    static const char request[] =
        "GET / HTTP/1.1\r\n"
        "Host: test\r\n"
        "Expect: 100-continue\r\n"
        "\r\n";

    TEST("an expectation with no body gets no 100");
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connect failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (sock_write_once(fd, request, sizeof(request) - 1) !=
        (ssize_t)(sizeof(request) - 1)) {
        FAIL("the request was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (read_head(fd, head, sizeof(head)) < 0) {
        FAIL("no response arrived");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(head, "HTTP/1.1 100", 12) == 0) {
        FAIL("a 100 was sent for a request with no body");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(head, "HTTP/1.1 200 OK", 15) != 0) {
        FAIL("the response was not a 200");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    sock_close(fd);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 4. THE BODY IS ALREADY THERE. RFC 9110 10.1.1: "A server MAY omit
 *    sending a 100 (Continue) response if it has already received some
 *    or all of the content for the corresponding request." */
static void test_the_body_already_there_gets_no_100(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char head[1024];
    char both[512];
    size_t n;

    TEST("the body already there gets no 100");
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connect failed");
        waitpid(pid, NULL, 0);
        return;
    }
    n = 0;
    memcpy(both + n, expect_head, sizeof(expect_head) - 1);
    n += sizeof(expect_head) - 1;
    memcpy(both + n, "hello", 5);
    n += 5;
    if (sock_write_once(fd, both, n) != (ssize_t)n) {
        FAIL("the request was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (read_head(fd, head, sizeof(head)) < 0) {
        FAIL("no response arrived");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(head, "HTTP/1.1 100", 12) == 0) {
        FAIL("a 100 was sent for a request whose body had arrived");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(head, "HTTP/1.1 200 OK", 15) != 0) {
        FAIL("the response was not a 200");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    sock_close(fd);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 5. NO EXPECTATION. The regression guard: the 100 is for a request
 *    that asked for it. */
static void test_no_expectation_gets_no_100(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char head[1024];
    char both[512];
    size_t n;
    static const char plain_head[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Length: 5\r\n"
        "\r\n";

    TEST("no expectation gets no 100");
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connect failed");
        waitpid(pid, NULL, 0);
        return;
    }
    n = 0;
    memcpy(both + n, plain_head, sizeof(plain_head) - 1);
    n += sizeof(plain_head) - 1;
    memcpy(both + n, "hello", 5);
    n += 5;
    if (sock_write_once(fd, both, n) != (ssize_t)n) {
        FAIL("the request was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (read_head(fd, head, sizeof(head)) < 0) {
        FAIL("no response arrived");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(head, "HTTP/1.1 100", 12) == 0) {
        FAIL("a 100 was sent for a request that did not ask for one");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(head, "HTTP/1.1 200 OK", 15) != 0) {
        FAIL("the response was not a 200");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    sock_close(fd);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 6. A REQUEST THE SERVER REFUSES. The final response satisfies the
 *    MUST; the 100 is for a request the server intends to serve. */
static void test_a_refused_request_gets_no_100(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char head[1024];
    static const char refused[] =
        "POST / HTTP/1.1\r\n"
        "Host: test\r\n"
        "Expect: 100-continue\r\n"
        "Content-Length: 5\r\n"
        "\r\n";

    TEST("a refused request gets no 100");
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connect failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (sock_write_once(fd, refused, sizeof(refused) - 1) !=
        (ssize_t)(sizeof(refused) - 1)) {
        FAIL("the head was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    /* Nothing yet: the server is waiting for the body, and it is not
     * going to say "go ahead" for a request it is going to refuse. */
    if (readable_in(fd, 300)) {
        FAIL("the server answered a refused request before the body");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (sock_write_once(fd, "hello", 5) != 5) {
        FAIL("the body was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (read_head(fd, head, sizeof(head)) < 0) {
        FAIL("no response arrived");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(head, "HTTP/1.1 100", 12) == 0) {
        FAIL("a 100 was sent for a refused request");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(head, "HTTP/1.1 405", 12) != 0) {
        FAIL("the refusal was not a 405");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    sock_close(fd);
    waitpid(pid, NULL, 0);
    PASS();
}

/* 7. THE BODY ARRIVES, AND THE CONNECTION STAYS IN SYNC. The interim
 *    response is a message on a connection that already has a request
 *    in flight: the final response must still be the final response,
 *    and the next request must still be answered. */
static void test_the_body_arrives_and_the_connection_stays_in_sync(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char interim[256];
    char head[1024];
    char body[1024];
    long clen;
    static const char second[] =
        "GET / HTTP/1.1\r\n"
        "Host: test\r\n"
        "\r\n";

    TEST("the body arrives and the connection stays in sync");
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connect failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (sock_write_once(fd, expect_head, sizeof(expect_head) - 1) !=
        (ssize_t)(sizeof(expect_head) - 1)) {
        FAIL("the head was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (read_head(fd, interim, sizeof(interim)) < 0 ||
        strcmp(interim, "HTTP/1.1 100 Continue\r\n\r\n") != 0) {
        FAIL("the interim response was not a bare 100 Continue");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (sock_write_once(fd, "hello", 5) != 5) {
        FAIL("the body was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (read_head(fd, head, sizeof(head)) < 0 ||
        strncmp(head, "HTTP/1.1 200 OK", 15) != 0) {
        FAIL("the final response was not a 200");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    clen = content_length_of(head);
    if (clen != 5 || read(fd, body, 5) != 5 || memcmp(body, "hello", 5) != 0) {
        FAIL("the body was not echoed");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    /* The next request on the same connection. */
    if (sock_write_once(fd, second, sizeof(second) - 1) !=
        (ssize_t)(sizeof(second) - 1)) {
        FAIL("the second request was not sent");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (read_head(fd, head, sizeof(head)) < 0 ||
        strncmp(head, "HTTP/1.1 200 OK", 15) != 0) {
        FAIL("the connection was not in sync");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    clen = content_length_of(head);
    if (clen != (long)strlen(our_body) ||
        read(fd, body, (size_t)clen) != (ssize_t)clen ||
        memcmp(body, our_body, (size_t)clen) != 0) {
        FAIL("the second response was wrong");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    sock_close(fd);
    waitpid(pid, NULL, 0);
    PASS();
}

int main(void)
{
    signal(SIGALRM, on_alarm);
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(30);

    printf("Stage 65.33 -- the 100 Continue\n");

    test_the_parser_says_the_head_is_complete_before_the_body();
    test_the_server_answers_a_100_before_the_body();
    test_an_expectation_with_no_body_gets_no_100();
    test_the_body_already_there_gets_no_100();
    test_no_expectation_gets_no_100();
    test_a_refused_request_gets_no_100();
    test_the_body_arrives_and_the_connection_stays_in_sync();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
