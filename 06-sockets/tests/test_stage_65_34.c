/*
 * Stage 65.34 -- the client's conditional request.
 *
 * Stage 65.32 gave the SERVER the rule: a GET carrying "If-None-Match"
 * whose condition is false is answered with a 304 (RFC 9110 13.1.2,
 * 15.4.5). But the rule had no requester -- nothing in this component
 * could ASK the question, so the server half of the conditional
 * request was a rule nobody could exercise.
 *
 * This stage gives the client the request that asks it:
 *
 *   - the request builder grows ONE extra-header slot, and the slot is
 *     checked: a value carrying a CR or an LF would end the header
 *     line and start another one;
 *   - the client grows a conditional GET.
 *
 * The oracle is our own server: the client reads the validator off a
 * 200 and sends it back, and the two halves of the component agree.
 */
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

/* Our own server, one connection. */
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

static void reap(pid_t pid)
{
    int st;
    if (pid > 0) {
        waitpid(pid, &st, 0);
    }
}

static void test_the_builder_writes_the_extra_header(void)
{
    char buf[1024];
    int n;

    TEST("the builder writes the extra header");
    n = http_client_request_build_with_header(buf, sizeof(buf), "GET", "/",
                                              "127.0.0.1", NULL, 0, 0,
                                              "If-None-Match", "W/\"abc\"");
    if (n < 0) {
        FAIL("the build failed");
        return;
    }
    if (strstr(buf, "If-None-Match: W/\"abc\"\r\n") == NULL) {
        FAIL("the header is not on the wire");
        return;
    }
    if (strncmp(buf, "GET / HTTP/1.1\r\n", 16) != 0) {
        FAIL("the request line moved");
        return;
    }
    PASS();
}

static void test_no_extra_header_is_the_same_bytes(void)
{
    char a[1024];
    char b[1024];
    int na;
    int nb;

    TEST("no extra header is the same bytes");
    na = http_client_request_build_with_header(a, sizeof(a), "GET", "/",
                                               "127.0.0.1", NULL, 0, 1,
                                               NULL, NULL);
    nb = http_client_request_build_ex(b, sizeof(b), "GET", "/",
                                      "127.0.0.1", NULL, 0, 1);
    if (na < 0 || nb < 0) {
        FAIL("a build failed");
        return;
    }
    if (na != nb || memcmp(a, b, (size_t)na) != 0) {
        FAIL("the two builders disagree");
        return;
    }
    PASS();
}

static void test_a_value_with_a_crlf_is_refused(void)
{
    char buf[1024];
    int n;

    TEST("a value with a CRLF is refused");
    errno = 0;
    n = http_client_request_build_with_header(buf, sizeof(buf), "GET", "/",
                                              "127.0.0.1", NULL, 0, 0,
                                              "If-None-Match", "a\r\nX-Evil: 1");
    if (n != -1 || errno != EINVAL) {
        FAIL("the injected header line was accepted");
        return;
    }
    PASS();
}

static void test_a_name_with_a_colon_is_refused(void)
{
    char buf[1024];
    int n;

    TEST("a name with a colon is refused");
    errno = 0;
    n = http_client_request_build_with_header(buf, sizeof(buf), "GET", "/",
                                              "127.0.0.1", NULL, 0, 0,
                                              "Bad:Name", "v");
    if (n != -1 || errno != EINVAL) {
        FAIL("the bad name was accepted");
        return;
    }
    PASS();
}

static void test_one_null_and_one_not_is_refused(void)
{
    char buf[1024];
    int n;

    TEST("one null and one not is refused");
    errno = 0;
    n = http_client_request_build_with_header(buf, sizeof(buf), "GET", "/",
                                              "127.0.0.1", NULL, 0, 0,
                                              "If-None-Match", NULL);
    if (n != -1 || errno != EINVAL) {
        FAIL("the half-given header was accepted");
        return;
    }
    PASS();
}

static void test_a_matching_etag_gets_a_304(void)
{
    pid_t pid;
    uint16_t port;
    http_response r;
    const char *etag;
    char tag[256];

    TEST("a matching validator gets a 304");
    port = spawn_our_server(&pid);
    if (http_client_get("127.0.0.1", port, "/", &r) != 0) {
        FAIL("the GET failed");
        reap(pid);
        return;
    }
    reap(pid);
    if (r.status != 200) {
        FAIL("the first GET was not a 200");
        return;
    }
    etag = http_response_header_get(&r, "ETag");
    if (etag == NULL) {
        FAIL("the 200 carried no validator");
        return;
    }
    snprintf(tag, sizeof(tag), "%s", etag);

    port = spawn_our_server(&pid);
    if (http_client_get_conditional("127.0.0.1", port, "/", tag, &r) != 0) {
        FAIL("the conditional GET failed");
        reap(pid);
        return;
    }
    reap(pid);
    if (r.status != 304) {
        FAIL("the conditional GET was not a 304");
        return;
    }
    if (r.body_len != 0) {
        FAIL("the 304 carried a body");
        return;
    }
    PASS();
}

static void test_a_tag_that_does_not_match_gets_the_200(void)
{
    pid_t pid;
    uint16_t port;
    http_response r;

    TEST("a tag that does not match gets the 200");
    port = spawn_our_server(&pid);
    if (http_client_get_conditional("127.0.0.1", port, "/",
                                    "W/\"fnv1a64-0000000000000000\"", &r) != 0) {
        FAIL("the conditional GET failed");
        reap(pid);
        return;
    }
    reap(pid);
    if (r.status != 200) {
        FAIL("the answer was not a 200");
        return;
    }
    if (r.body_len != strlen(our_body)) {
        FAIL("the body was not delivered");
        return;
    }
    PASS();
}

int main(void)
{
    signal(SIGALRM, on_alarm);
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(60);

    printf("Stage 65.34 -- the client's conditional request\n");

    test_the_builder_writes_the_extra_header();
    test_no_extra_header_is_the_same_bytes();
    test_a_value_with_a_crlf_is_refused();
    test_a_name_with_a_colon_is_refused();
    test_one_null_and_one_not_is_refused();
    test_a_matching_etag_gets_a_304();
    test_a_tag_that_does_not_match_gets_the_200();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
