/*
 * Stage 65.29 -- 405 Method Not Allowed.
 *
 * The rules:
 *
 * RFC 9110 15.5.6:
 *
 *   "The 405 (Method Not Allowed) status code indicates that the method
 *   received in the request-line is known by the origin server but not
 *   supported by the target resource. The origin server MUST generate an
 *   Allow header field in a 405 response containing a list of the target
 *   resource's currently supported methods."
 *
 * RFC 9110 15.6.2:
 *
 *   "The 501 (Not Implemented) status code indicates that the server does
 *   not support the functionality required to fulfill the request. This
 *   is the appropriate response when the server does not recognize the
 *   request method and is not capable of supporting it for any
 *   resource."
 *
 * RFC 9110 9.1:
 *
 *   "The method token is case-sensitive because it might be used as a
 *   gateway to object-based systems with case-sensitive method names."
 *
 *   "...though each resource determines for itself whether those
 *   semantics are implemented or allowed."
 *
 * Three rules, not one, and they answer three different questions:
 *
 *   - Does the target resource exist?  No -> 404 (there is no resource
 *     whose methods could be listed, so there is no Allow header).
 *
 *   - Does the server know the method at all?  No -> 501. The server
 *     "does not recognize the request method and is not capable of
 *     supporting it for any resource".
 *
 *   - Does THIS resource support the method?  No -> 405, and the Allow
 *     header is a MUST: it names the resource's supported methods.
 *
 * What shipped before this stage: none of it. The server answered every
 * method with the same response -- a POST to / got the document, and a
 * made-up method got the document too. The method was parsed and then
 * ignored.
 *
 * What this stage must NOT break: the request body. A POST to a
 * resource that does not support POST still has a body, and that body
 * was framed and consumed by the parser -- so the connection is still
 * in sync and the refusal does not have to close it. That is the
 * difference between this refusal and the parser-level refusals of
 * Stages 65.21-65.23: those refused the MESSAGE, so the body was never
 * framed and the connection could not be reused. This one refuses the
 * REQUEST, and the message was fine.
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

/* A peer that sends a canned response and then HOLDS the connection
 * open for hold_ms. */
static uint16_t spawn_holding_peer_ms(pid_t *pid_out, const char *response, int hold_ms)
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
            ssize_t n = read(fd, buf, sizeof(buf));
            (void)n;
            (void)write(fd, response, strlen(response));
            usleep((useconds_t)hold_ms * 1000);
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

/* 1. The ordinary case is untouched: a GET is still a GET. */
static void test_get_is_still_a_get(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("GET / is still answered with the document");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the status is not 200");
        return;
    }
    if (!contains(resp, our_body)) {
        FAIL("the document did not come back");
        return;
    }
    PASS();
}

/* 2. A known method the resource does not support: 405, and the Allow
 *    header naming the methods it does support. */
static void test_post_to_a_get_only_resource_is_405(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("POST / is 405 with Allow: GET, HEAD");
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
    if (!contains(resp, "Allow: GET, HEAD\r\n")) {
        FAIL("the Allow header does not name the supported methods");
        return;
    }
    if (!contains(resp, "Content-Length: 23\r\n")) {
        FAIL("the length does not match the body");
        return;
    }
    if (!contains(resp, "405 Method Not Allowed\n")) {
        FAIL("the body is not the refusal");
        return;
    }
    PASS();
}

/* 3. The resource that DOES support POST still does: the refusal is
 *    per resource, not per server. */
static void test_post_to_echo_is_still_answered(void)
{
    char req[512];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("POST /echo is still answered with the body");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "POST /echo HTTP/1.1\r\nHost: 127.0.0.1\r\n"
             "Content-Length: 5\r\nConnection: close\r\n\r\nhello");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) != 0) {
        FAIL("the status is not 200");
        return;
    }
    if (!contains(resp, "hello")) {
        FAIL("the body was not echoed");
        return;
    }
    PASS();
}

/* 4. A method the server cannot do for ANY resource: 501, not 405.
 *
 *    RFC 9110 15.6.2: 501 "is the appropriate response when the server
 *    does not recognize the request method and is not capable of
 *    supporting it for any resource." DELETE is a method this server
 *    implements nowhere, so there is no resource whose methods could
 *    be listed -- and no Allow header.
 *
 *    This is the other side of the 405 rule: POST is supported by
 *    /echo and not by /, so a POST to / is a 405 with an Allow header;
 *    DELETE is supported nowhere, so it is a 501. */
static void test_a_method_the_server_cannot_do_anywhere_is_501(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("DELETE /echo is 501 -- the server cannot do DELETE at all");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "DELETE /echo HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
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
    if (contains(resp, "Allow:")) {
        FAIL("the 501 carries an Allow header");
        return;
    }
    PASS();
}

/* 4b. The method set is per resource, and the list is the one the 405
 *     would name. /echo supports POST; / and /stream do not. */
static void test_the_method_set_is_per_resource(void)
{
    char allow[64];
    int n;

    TEST("the method set is per resource (/echo has POST, / does not)");
    n = http_resource_allow(allow, sizeof(allow), "/");
    if (n < 0 || strcmp(allow, "GET, HEAD") != 0) {
        FAIL("/ does not support GET and HEAD");
        return;
    }
    n = http_resource_allow(allow, sizeof(allow), "/stream");
    if (n < 0 || strcmp(allow, "GET, HEAD") != 0) {
        FAIL("/stream does not support GET and HEAD");
        return;
    }
    n = http_resource_allow(allow, sizeof(allow), "/echo");
    if (n < 0 || strcmp(allow, "GET, HEAD, POST") != 0) {
        FAIL("/echo does not support GET, HEAD and POST");
        return;
    }
    n = http_resource_allow(allow, sizeof(allow), "/nope");
    if (n != 0 || allow[0] != '\0') {
        FAIL("a target that is not a resource has no methods");
        return;
    }
    PASS();
}

/* 5. A method the server does not know: 501, and no Allow header (the
 *    MUST is on a 405 response). */
static void test_an_unknown_method_is_501(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("FROB / is 501 Not Implemented");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "FROB / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
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
    if (contains(resp, "Allow:")) {
        FAIL("the 501 carries an Allow header");
        return;
    }
    PASS();
}

/* 6. The path decides first: a method the server does not know, on a
 *    path that is not there, is a 404 -- there is no resource whose
 *    methods could be listed. */
static void test_an_unknown_method_on_a_missing_path_is_404(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("FROB /nope is 404, not 501");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "FROB /nope HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
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

/* 7. A known method on a missing path is a 404 too -- the resource
 *    does not exist, whatever the method. */
static void test_a_known_method_on_a_missing_path_is_404(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("PUT /nope is 404, not 405");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "PUT /nope HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
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

/* 8. The method token is case-sensitive: "get" is not a method this
 *    server knows, so it is not a GET either. */
static void test_the_method_token_is_case_sensitive(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("get / is 501 -- the method token is case-sensitive");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "get / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
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

/* 9. The 405 does not close the connection: the request was fine, its
 *    body was framed and consumed, and the conversation can go on. */
static void test_the_405_keeps_the_connection(void)
{
    char req[512];
    char resp1[4096];
    char resp2[4096];
    ssize_t n1;
    ssize_t n2;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("the 405 keeps the connection: a second request is answered");
    alarm(10);
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    snprintf(req, sizeof(req),
             "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n");
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
    if (strncmp(resp1, "HTTP/1.1 405 Method Not Allowed\r\n", 32) != 0) {
        FAIL("the first response is not 405");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (!contains(resp1, "Connection: keep-alive\r\n")) {
        /* The header is a claim about the connection, and the server
         * keeps it: a "close" here would be a lie the wire would
         * disprove. */
        FAIL("the 405 does not say the connection is kept");
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

/* 10. The refused request's body was still consumed: the bytes after
 *     it are the next request, not part of this one. */
static void test_the_405_consumed_the_request_body(void)
{
    char req[512];
    char resp1[4096];
    char resp2[4096];
    ssize_t n1;
    ssize_t n2;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("the 405 consumed the body: the next request is answered");
    alarm(10);
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    snprintf(req, sizeof(req),
             "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\n"
             "Content-Length: 5\r\nConnection: keep-alive\r\n\r\nhello");
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
    if (strncmp(resp1, "HTTP/1.1 405 Method Not Allowed\r\n", 32) != 0) {
        FAIL("the first response is not 405");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (!contains(resp1, "Connection: keep-alive\r\n")) {
        /* The header is a claim about the connection, and the server
         * keeps it: a "close" here would be a lie the wire would
         * disprove. */
        FAIL("the 405 does not say the connection is kept");
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

/* 11. The 501 keeps the connection too: the message was fine, only the
 *     method was not understood. */
static void test_the_501_keeps_the_connection(void)
{
    char req[512];
    char resp1[4096];
    char resp2[4096];
    ssize_t n1;
    ssize_t n2;
    pid_t pid;
    uint16_t port;
    int fd;

    TEST("the 501 keeps the connection: a second request is answered");
    alarm(10);
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    snprintf(req, sizeof(req),
             "FROB / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n");
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
    if (strncmp(resp1, "HTTP/1.1 501 Not Implemented\r\n", 29) != 0) {
        FAIL("the first response is not 501");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    if (!contains(resp1, "Connection: keep-alive\r\n")) {
        FAIL("the 501 does not say the connection is kept");
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

/* 12. The client's half: a 405 is an ordinary response. The status, the
 *     reason, the body, and the Allow header all arrive. */
static void test_our_client_reads_a_405(void)
{
    static const char canned[] =
        "HTTP/1.1 405 Method Not Allowed\r\n"
        "Date: Thu, 12 Nov 2026 00:00:00 GMT\r\n"
        "Content-Type: text/plain\r\n"
        "Allow: GET, HEAD\r\n"
        "Content-Length: 23\r\n"
        "Connection: close\r\n"
        "\r\n"
        "405 Method Not Allowed\n";
    pid_t pid;
    uint16_t port;
    http_client_connection c;
    http_response r;
    struct timeval tv;
    int i;
    const char *allow = NULL;

    TEST("our client reads a 405: status, body, and the Allow header");
    alarm(10);
    port = spawn_holding_peer_ms(&pid, canned, 2000);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    (void)setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (http_client_connection_request(&c, "POST", "/", NULL, 0, &r) != 0) {
        FAIL("the client did not get the response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    if (r.status != 405) {
        FAIL("the status is not 405");
        return;
    }
    if (strcmp(r.reason, "Method Not Allowed") != 0) {
        FAIL("the reason is not Method Not Allowed");
        return;
    }
    if (r.body_len != 23 || memcmp(r.body, "405 Method Not Allowed\n", 23) != 0) {
        FAIL("the body did not come back");
        return;
    }
    for (i = 0; i < r.nheaders; i++) {
        if (strcasecmp(r.headers[i].name, "Allow") == 0) {
            allow = r.headers[i].value;
        }
    }
    if (allow == NULL || strcmp(allow, "GET, HEAD") != 0) {
        FAIL("the Allow header did not come back");
        return;
    }
    PASS();
}

/* 13. An Allow header only where it belongs: not on the 200, not on
 *     the 404, not on the 501. */
static void test_allow_only_on_a_405(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("the 200 and the 404 carry no Allow header");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (contains(resp, "Allow:")) {
        FAIL("the 200 carries an Allow header");
        return;
    }
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "GET /nope HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (contains(resp, "Allow:")) {
        FAIL("the 404 carries an Allow header");
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

    test_get_is_still_a_get();
    test_post_to_a_get_only_resource_is_405();
    test_post_to_echo_is_still_answered();
    test_a_method_the_server_cannot_do_anywhere_is_501();
    test_the_method_set_is_per_resource();
    test_an_unknown_method_is_501();
    test_an_unknown_method_on_a_missing_path_is_404();
    test_a_known_method_on_a_missing_path_is_404();
    test_the_method_token_is_case_sensitive();
    test_the_405_keeps_the_connection();
    test_the_405_consumed_the_request_body();
    test_the_501_keeps_the_connection();
    test_our_client_reads_a_405();
    test_allow_only_on_a_405();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
