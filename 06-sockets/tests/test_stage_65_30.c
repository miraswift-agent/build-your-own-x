/*
 * Stage 65.30 -- OPTIONS.
 *
 * The rules:
 *
 * RFC 9110 9.3.7:
 *
 *   "The OPTIONS method requests information about the communication
 *   options available for the target resource, at either the origin
 *   server or an intervening intermediary. This method allows a client
 *   to determine the options and/or requirements associated with a
 *   resource, or the capabilities of a server, without implying a
 *   resource action."
 *
 *   "If the request target is not an asterisk, the OPTIONS request
 *   applies to the options that are available when communicating with
 *   the target resource."
 *
 *   "A server generating a successful response to OPTIONS SHOULD send
 *   any header that might indicate optional features implemented by the
 *   server and applicable to the target resource (e.g., Allow)..."
 *
 * RFC 9110 10.2.1 (the Allow field):
 *
 *   "The 'Allow' header field lists the set of methods advertised as
 *   supported by the target resource."
 *
 * RFC 9110 8.3:
 *
 *   "A sender SHOULD NOT generate a Content-Type header field in a
 *   message that does not contain content."
 *
 * What Stage 65.29 left behind: the method table knew which methods
 * each resource supports, and the 405 carried the list. But the list
 * could not be asked for directly -- OPTIONS was not a method this
 * server knew, so OPTIONS / was a 501. The one list that could not
 * reach the wire was the one a client actually wants: "what can I do
 * with this resource?"
 *
 * Two questions this stage answers:
 *
 *   - What does an OPTIONS request get? A 200 with the Allow header
 *     naming the target resource's methods, and no content. The RFC
 *     asks for "any header that might indicate optional features
 *     implemented by the server and applicable to the target resource
 *     (e.g., Allow)" -- so the Allow header is the answer, and it is
 *     the same list the 405 carries.
 *
 *   - Does OPTIONS belong in that list? Yes: after this stage the
 *     server supports OPTIONS for every resource it serves, and the
 *     Allow field "lists the set of methods advertised as supported by
 *     the target resource". So OPTIONS is in every resource's set,
 *     and the 405's list grows with it. One table, so the decision and
 *     the list cannot drift apart.
 *
 * What this stage must NOT break:
 *
 *   - The framing. The OPTIONS response has Content-Length: 0, so it
 *     is framed the ordinary way -- unlike a HEAD response, whose
 *     Content-Length describes another message. No client change.
 *
 *   - The request body. An OPTIONS request may carry content (RFC 9110
 *     9.3.7 allows it, and requires the client to describe it with a
 *     Content-Type). The body is framed and consumed by the parser, so
 *     the connection stays in sync.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

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

/* A hang is a poor failure mode: if a peer waits for something that
 * never comes, this fires and the test fails with a reason. */
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

/* ---------- helpers ---------- */

static int contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

/* The value of a header in a response head, or NULL. The head is
 * NUL-terminated by the caller. */
static const char *head_value(const char *head, const char *name)
{
    size_t nlen = strlen(name);
    const char *p = head;

    while ((p = strstr(p, name)) != NULL) {
        /* the name must start a line */
        if (p != head && p[-1] != '\n') {
            p += nlen;
            continue;
        }
        if (p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            return v;
        }
        p += nlen;
    }
    return NULL;
}

/* Our own server, for the request side of the rule. */
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

static int write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* Read one HTTP response: the head (up to the empty line that ends it),
 * then the body the head promises. Returns the number of bytes read, or
 * -1. This is the boundary a keep-alive connection needs: the server
 * does not close, so the length is the only end of the message. */
static ssize_t read_one_response(int fd, char *out, size_t out_cap)
{
    size_t got = 0;
    size_t head_end = 0;
    long body_len = 0;
    const char *cl;

    while (got + 1 < out_cap && head_end == 0) {
        ssize_t n = read(fd, out + got, out_cap - got - 1);
        size_t i;
        if (n > 0) {
            got += (size_t)n;
        } else if (n == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            break;
        }
        for (i = 0; i + 4 <= got; i++) {
            if (memcmp(out + i, "\r\n\r\n", 4) == 0) {
                head_end = i + 4;
                break;
            }
        }
    }
    if (head_end == 0) {
        return -1;
    }
    out[head_end] = '\0';
    cl = head_value(out, "Content-Length");
    if (cl != NULL) {
        body_len = strtol(cl, NULL, 10);
    }
    /* The body may already be in the buffer -- the server writes the
     * head and the body together -- so the length is checked BEFORE
     * reading again. Reading first would block on a keep-alive
     * connection whose body had already arrived. */
    while ((long)(got - head_end) < body_len) {
        ssize_t n;
        if (got + (size_t)body_len + 1 >= out_cap) {
            break;
        }
        n = read(fd, out + got, out_cap - got - 1);
        if (n > 0) {
            got += (size_t)n;
        } else if (n == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    out[got] = '\0';
    return (ssize_t)got;
}

/* Send raw request bytes and read everything the server sends back
 * until it closes. Returns the number of bytes read, or -1. */
static ssize_t raw_exchange(uint16_t port, const char *request,
                             char *out, size_t out_cap)
{
    int fd = sock_connect_loopback(port);
    size_t got = 0;

    if (fd < 0) {
        return -1;
    }
    if (write_all(fd, request, strlen(request)) != 0) {
        sock_close(fd);
        return -1;
    }
    for (;;) {
        ssize_t n;
        if (got + 1 >= out_cap) {
            break;
        }
        n = read(fd, out + got, out_cap - got - 1);
        if (n > 0) {
            got += (size_t)n;
            continue;
        }
        if (n == 0) {
            break;              /* the server closed: the exchange is over */
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }
    out[got] = '\0';
    sock_close(fd);
    return (ssize_t)got;
}

/* ---------- the server's half ---------- */

/* 1. The ordinary case: OPTIONS on a resource the server serves. */
static void test_options_on_the_root_is_200(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("OPTIONS / is 200 with the resource's methods");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "OPTIONS / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the status is not 200 OK");
        return;
    }
    if (!contains(resp, "Allow: GET, HEAD, OPTIONS\r\n")) {
        FAIL("the Allow header does not name the resource's methods");
        return;
    }
    if (!contains(resp, "Content-Length: 0\r\n")) {
        FAIL("the length is not zero -- the response has content");
        return;
    }
    if (!contains(resp, "\r\n\r\n")) {
        FAIL("the head did not end");
        return;
    }
    PASS();
}

/* 2. The OPTIONS response has no content, so it has no Content-Type.
 *
 *    RFC 9110 8.3: "A sender SHOULD NOT generate a Content-Type header
 *    field in a message that does not contain content." */
static void test_the_options_response_has_no_content_type(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("the OPTIONS response carries no Content-Type");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "OPTIONS / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (contains(resp, "Content-Type:")) {
        FAIL("a response with no content describes a media type");
        return;
    }
    PASS();
}

/* 3. The list is per resource: /echo supports POST, / does not. */
static void test_the_options_list_is_per_resource(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("OPTIONS /echo lists POST; OPTIONS /stream does not");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "OPTIONS /echo HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (!contains(resp, "Allow: GET, HEAD, POST, OPTIONS\r\n")) {
        FAIL("/echo does not list POST");
        return;
    }
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "OPTIONS /stream HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (!contains(resp, "Allow: GET, HEAD, OPTIONS\r\n")) {
        FAIL("/stream lists a method it does not support");
        return;
    }
    PASS();
}

/* 4. The accessor and the wire agree: the list the server sends is the
 *    list the table holds. */
static void test_the_accessor_agrees_with_the_wire(void)
{
    char allow[64];
    int n;

    TEST("http_resource_allow agrees with the list on the wire");
    n = http_resource_allow(allow, sizeof(allow), "/");
    if (n < 0 || strcmp(allow, "GET, HEAD, OPTIONS") != 0) {
        FAIL("/ does not support GET, HEAD and OPTIONS");
        return;
    }
    n = http_resource_allow(allow, sizeof(allow), "/stream");
    if (n < 0 || strcmp(allow, "GET, HEAD, OPTIONS") != 0) {
        FAIL("/stream does not support GET, HEAD and OPTIONS");
        return;
    }
    n = http_resource_allow(allow, sizeof(allow), "/echo");
    if (n < 0 || strcmp(allow, "GET, HEAD, POST, OPTIONS") != 0) {
        FAIL("/echo does not support GET, HEAD, POST and OPTIONS");
        return;
    }
    n = http_resource_allow(allow, sizeof(allow), "/nope");
    if (n != 0 || allow[0] != '\0') {
        FAIL("a target that is not a resource has no methods");
        return;
    }
    PASS();
}

/* 5. A target that is not there: 404, and no Allow header -- there is
 *    no resource whose methods could be listed. */
static void test_options_on_a_missing_target_is_404(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("OPTIONS /nope is 404, with no Allow header");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "OPTIONS /nope HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 404 Not Found\r\n", 24) != 0) {
        FAIL("the status is not 404 Not Found");
        return;
    }
    if (contains(resp, "Allow:")) {
        FAIL("the 404 carries an Allow header");
        return;
    }
    PASS();
}

/* 6. The method token is still case-sensitive: "options" is not
 *    OPTIONS (RFC 9110 9.1). */
static void test_the_method_token_is_case_sensitive(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("options / is 501 -- the method token is case-sensitive");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "options / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 501 Not Implemented\r\n", 29) != 0) {
        FAIL("the status is not 501 Not Implemented");
        return;
    }
    PASS();
}

/* 7. The 405's list grew, because OPTIONS is now supported for every
 *    resource. One table: the decision and the list cannot drift. */
static void test_the_405_lists_options_too(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("POST / is 405 with Allow: GET, HEAD, OPTIONS");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 405 Method Not Allowed\r\n", 32) != 0) {
        FAIL("the status is not 405 Method Not Allowed");
        return;
    }
    if (!contains(resp, "Allow: GET, HEAD, OPTIONS\r\n")) {
        FAIL("the Allow header does not list OPTIONS");
        return;
    }
    PASS();
}

/* 8. The OPTIONS response keeps the connection: the request was fine,
 *    and its body was framed and consumed. */
static void test_the_options_response_keeps_the_connection(void)
{
    char req[512];
    char resp1[4096];
    char resp2[4096];
    ssize_t n1;
    ssize_t n2;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("the OPTIONS response keeps the connection: a second request works");
    alarm(10);
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    snprintf(req, sizeof(req),
             "OPTIONS / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n");
    if (write_all(fd, req, strlen(req)) != 0) {
        FAIL("the request did not go out");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    n1 = read_one_response(fd, resp1, sizeof(resp1));
    if (n1 < 0) {
        FAIL("the first response did not arrive");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(resp1, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the first response is not 200");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (!contains(resp1, "Connection: keep-alive\r\n")) {
        FAIL("the response does not say the connection is kept");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    snprintf(req, sizeof(req),
             "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    if (write_all(fd, req, strlen(req)) != 0) {
        FAIL("the second request did not go out");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    n2 = read_one_response(fd, resp2, sizeof(resp2));
    sock_close(fd);
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n2 < 0) {
        FAIL("the second response did not arrive -- the server closed");
        return;
    }
    if (strncmp(resp2, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the second response is not 200");
        return;
    }
    PASS();
}

/* 9. An OPTIONS request may carry content (RFC 9110 9.3.7 allows it),
 *    and the body must be consumed -- the bytes after it belong to the
 *    next message, not to this one. */
static void test_an_options_request_body_is_consumed(void)
{
    char req[512];
    char resp1[4096];
    char resp2[4096];
    ssize_t n1;
    ssize_t n2;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("the OPTIONS request body was consumed: the next request works");
    alarm(10);
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    snprintf(req, sizeof(req),
             "OPTIONS /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n"
             "Content-Type: text/plain\r\nContent-Length: 5\r\n"
             "Connection: keep-alive\r\n\r\nhello");
    if (write_all(fd, req, strlen(req)) != 0) {
        FAIL("the request did not go out");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    n1 = read_one_response(fd, resp1, sizeof(resp1));
    if (n1 < 0) {
        FAIL("the first response did not arrive");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (strncmp(resp1, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the first response is not 200");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (contains(resp1, "hello")) {
        FAIL("the OPTIONS response echoed the request's content");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    snprintf(req, sizeof(req),
             "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    if (write_all(fd, req, strlen(req)) != 0) {
        FAIL("the second request did not go out");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    n2 = read_one_response(fd, resp2, sizeof(resp2));
    sock_close(fd);
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n2 < 0) {
        FAIL("the second response did not arrive -- the body was not consumed");
        return;
    }
    if (strncmp(resp2, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the second response is not 200");
        return;
    }
    PASS();
}

/* 10. The client's half: an OPTIONS request is an ordinary request, and
 *     the response is an ordinary response -- the framing is ordinary
 *     too (Content-Length: 0), so no client change was needed. */
static void test_our_client_can_ask_and_read_the_answer(void)
{
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    struct timeval tv;
    int i;
    const char *allow = NULL;

    TEST("our client asks OPTIONS and reads the Allow header back");
    alarm(10);
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    (void)setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (http_client_connection_request(&c, "OPTIONS", "/", NULL, 0, &r) != 0) {
        FAIL("the client did not get the response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    if (r.status != 200) {
        FAIL("the status is not 200");
        return;
    }
    if (r.body_len != 0) {
        FAIL("the response has a body");
        return;
    }
    if (r.content_length != 0) {
        FAIL("the content length is not zero");
        return;
    }
    for (i = 0; i < r.nheaders; i++) {
        if (strcasecmp(r.headers[i].name, "Allow") == 0) {
            allow = r.headers[i].value;
        }
    }
    if (allow == NULL || strcmp(allow, "GET, HEAD, OPTIONS") != 0) {
        FAIL("the Allow header did not come back");
        return;
    }
    PASS();
}

/* 11. Nothing else moved: a GET is still a GET, and a HEAD is still
 *     the GET's head with no content. */
static void test_get_and_head_are_unchanged(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("GET and HEAD are unchanged");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the GET exchange failed");
        return;
    }
    if (!contains(resp, our_body)) {
        FAIL("the document did not come back");
        return;
    }
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "HEAD / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the HEAD exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the HEAD status is not 200");
        return;
    }
    if (contains(resp, our_body)) {
        FAIL("the HEAD response carried content");
        return;
    }
    if (!contains(resp, "Content-Length: 22\r\n")) {
        FAIL("the HEAD response does not carry the GET's length");
        return;
    }
    PASS();
}

/* 12. The routed server answers OPTIONS too: the method dispatch lives
 *     in the shared serving loop, not in one entry point. */
static void test_the_routed_server_answers_options(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;
    sock_listener l;

    TEST("the routed server answers OPTIONS with the same list");
    alarm(10);
    if (sock_listen_loopback(&l, 0, 8) != 0) {
        FAIL("the listener did not open");
        return;
    }
    pid = fork();
    if (pid < 0) {
        FAIL("the fork failed");
        return;
    }
    if (pid == 0) {
        http_host_route routes[1];
        http_server_stats st;
        int fd;
        routes[0].host = "example.com";
        routes[0].body = our_body;
        fd = sock_accept(&l);
        if (fd >= 0) {
            http_serve_connection_routed(fd, routes, 1, our_body, &st);
            sock_close(fd);
        }
        _exit(0);
    }
    port = l.port;
    snprintf(req, sizeof(req),
             "OPTIONS / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the status is not 200 OK");
        return;
    }
    if (!contains(resp, "Allow: GET, HEAD, OPTIONS\r\n")) {
        FAIL("the Allow header did not come back");
        return;
    }
    PASS();
}

int main(void)
{
    /* Unbuffered stdout: a test that dies on an alarm must have
     * already said which test it was in. */
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGALRM, on_alarm);

    test_options_on_the_root_is_200();
    test_the_options_response_has_no_content_type();
    test_the_options_list_is_per_resource();
    test_the_accessor_agrees_with_the_wire();
    test_options_on_a_missing_target_is_404();
    test_the_method_token_is_case_sensitive();
    test_the_405_lists_options_too();
    test_the_options_response_keeps_the_connection();
    test_an_options_request_body_is_consumed();
    test_our_client_can_ask_and_read_the_answer();
    test_get_and_head_are_unchanged();
    test_the_routed_server_answers_options();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
