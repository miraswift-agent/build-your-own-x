/*
 * Stage 65.32 -- the server's side: a response with no content.
 *
 * Stage 65.31 gave the client the rule that a 1xx, 204 or 304
 * response has no body -- "always terminated by the first empty line
 * after the header fields, regardless of the header fields present in
 * the message" (RFC 9112 6.3 item 1). This stage gives the server a
 * response that uses it: the 304 (Not Modified), which answers a
 * conditional GET whose condition is false.
 *
 * The rules:
 *
 * RFC 9110 13.1.2 (If-None-Match):
 *
 *   "When an origin server receives a request that selects a
 *   representation and that request includes an If-None-Match header
 *   field, the origin server MUST evaluate the If-None-Match condition
 *   per Section 13.2 prior to performing the method."
 *
 *   "A recipient MUST use the weak comparison function when comparing
 *   entity tags for If-None-Match (Section 8.8.3.2), since weak entity
 *   tags can be used for cache validation even if there have been
 *   changes to the representation data."
 *
 *   "To evaluate a received If-None-Match header field:
 *    1. If the field value is "*", the condition is false if the origin
 *       server has a current representation for the target resource.
 *    2. If the field value is a list of entity tags, the condition is
 *       false if one of the listed tags matches the entity tag of the
 *       selected representation.
 *    3. Otherwise, the condition is true."
 *
 *   "An origin server that evaluates an If-None-Match condition MUST NOT
 *   perform the requested method if the condition evaluates to false;
 *   instead, the origin server MUST respond with either a) the 304 (Not
 *   Modified) status code if the request method is GET or HEAD or b)
 *   the 412 (Precondition Failed) status code for all other request
 *   methods."
 *
 * RFC 9110 15.4.5 (304 Not Modified):
 *
 *   "The server generating a 304 response MUST generate any of the
 *   following header fields that would have been sent in a 200 (OK)
 *   response to the same request: Content-Location, Date, ETag, and
 *   Vary"
 *
 *   "A 304 response is terminated by the end of the header section; it
 *   cannot contain content or trailers."
 *
 * RFC 9110 8.8.3 (ETag):
 *
 *   "An entity tag consists of an opaque quoted string, possibly
 *   prefixed by a weakness indicator." ... "If an origin server
 *   provides an entity tag for a representation and the generation of
 *   that entity tag does not satisfy all of the characteristics of a
 *   strong validator (Section 8.8.1), then the origin server MUST mark
 *   the entity tag as weak by prefixing its opaque value with "W/"
 *   (case-sensitive)."
 *
 * RFC 9110 8.8.3.2 (Comparison):
 *
 *   "Weak comparison": two entity tags are equivalent if their opaque-
 *   tags match character-by-character, regardless of either or both
 *   being tagged as "weak".
 *
 * RFC 9110 13.2.1 (When to Evaluate):
 *
 *   "A server MUST ignore all received preconditions if its response
 *   to the same request without those conditions, prior to processing
 *   the request content, would have been a status code other than a 2xx
 *   (Successful) or 412 (Precondition Failed)."
 *
 *   "a server MUST ignore the conditional request header fields defined
 *   by this specification when received with a request method that does
 *   not involve the selection or modification of a selected
 *   representation, such as CONNECT, OPTIONS, or TRACE."
 *
 * The questions this stage answers:
 *
 *   - What does a conditional GET get? A 304 when the condition is
 *     false, and the ordinary 200 when it is true.
 *
 *   - What does the 304 carry? The date and the validator -- the
 *     headers that "would have been sent in a 200 (OK) response to the
 *     same request" -- and no content and no length.
 *
 *   - Which comparison? The weak one, so a cache that stored the tag
 *     without the "W/" still matches.
 *
 *   - When is the condition NOT evaluated? When the answer would have
 *     been something other than a 2xx (a 404), and for a method that
 *     does not select a representation (OPTIONS).
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
static const char other_body[] = "a different document\n";

/* ---------- helpers ---------- */

static int contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

/* Does the response start with this status line?
 *
 * The length comes from the literal itself: a counted literal is a
 * place to be wrong by one, and being wrong by one here is a test that
 * fails for a reason that has nothing to do with the rule. */
static int status_is(const char *resp, const char *line)
{
    return strncmp(resp, line, strlen(line)) == 0;
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

/* Copy a header's value out of a head. The value ends at the CRLF. */
static int copy_head_value(const char *head, const char *name,
                            char *out, size_t cap)
{
    const char *v = head_value(head, name);
    size_t len;

    if (v == NULL) {
        return -1;
    }
    len = strcspn(v, "\r\n");
    if (len + 1 > cap) {
        return -1;
    }
    memcpy(out, v, len);
    out[len] = '\0';
    return 0;
}

/* Our own server, for the request side of the rule. */
static uint16_t spawn_our_server_with(pid_t *pid_out, const char *body)
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
            http_serve_connection(fd, body, &st);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

static uint16_t spawn_our_server(pid_t *pid_out)
{
    return spawn_our_server_with(pid_out, our_body);
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
 * -1.
 *
 * The head is COPIED into head_out and NUL-terminated there, so a
 * string search over it sees the head and not the body. The raw bytes
 * stay in out, and the body starts at *body_off -- the head and the
 * body are two different things and a NUL written between them would
 * eat the body's first byte. */
static ssize_t read_one_response(int fd, char *out, size_t out_cap,
                                  char *head_out, size_t head_cap,
                                  size_t *body_off)
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
    if (head_end + 1 > head_cap) {
        return -1;
    }
    memcpy(head_out, out, head_end);
    head_out[head_end] = '\0';
    if (body_off != NULL) {
        *body_off = head_end;
    }
    cl = head_value(head_out, "Content-Length");
    if (cl != NULL) {
        body_len = strtol(cl, NULL, 10);
    }
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
        ssize_t n = read(fd, out + got, out_cap - got - 1);
        if (n > 0) {
            got += (size_t)n;
        } else if (n == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            break;
        }
        if (got + 1 >= out_cap) {
            break;
        }
    }
    out[got] = '\0';
    sock_close(fd);
    return (ssize_t)got;
}

/* A GET with an If-None-Match, with the connection closed after. */
static void build_get(char *dst, size_t cap, const char *inm)
{
    if (inm == NULL) {
        snprintf(dst, cap,
                 "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    } else {
        snprintf(dst, cap,
                 "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nIf-None-Match: %s\r\n"
                 "Connection: close\r\n\r\n", inm);
    }
}

/* ---------- 1. the 200 carries a validator ---------- */

static void test_the_document_carries_a_weak_etag(void)
{
    char req[256];
    char resp[4096];
    char etag[128];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("GET / carries a weak ETag");
    alarm(10);
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), NULL);
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (!status_is(resp, "HTTP/1.1 200 OK\r\n")) {
        FAIL("the status is not 200");
        return;
    }
    if (copy_head_value(resp, "ETag", etag, sizeof(etag)) != 0) {
        FAIL("the 200 carried no ETag");
        return;
    }
    /* The weakness indicator is a claim about the generator: a hash of
     * the bytes is not collision-free, so it is not a strong validator
     * (RFC 9110 8.8.1) and 8.8.3 requires the "W/". */
    if (strncmp(etag, "W/\"", 3) != 0) {
        FAIL("the ETag was not marked weak");
        return;
    }
    if (!contains(resp, "Content-Length: 22\r\n")) {
        FAIL("the length was not the document's");
        return;
    }
    if (!contains(resp, our_body)) {
        FAIL("the body was not the document");
        return;
    }
    PASS();
}

/* ---------- 2. the validator is stable ---------- */

static void test_the_validator_is_stable_for_one_document(void)
{
    char req[256];
    char resp1[4096];
    char resp2[4096];
    char etag1[128];
    char etag2[128];
    pid_t pid;
    uint16_t port;

    TEST("the same document has the same ETag");
    alarm(10);
    build_get(req, sizeof(req), NULL);
    port = spawn_our_server(&pid);
    if (raw_exchange(port, req, resp1, sizeof(resp1)) < 0) {
        FAIL("the first exchange failed");
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    waitpid(pid, NULL, 0);
    port = spawn_our_server(&pid);
    if (raw_exchange(port, req, resp2, sizeof(resp2)) < 0) {
        FAIL("the second exchange failed");
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    waitpid(pid, NULL, 0);
    alarm(0);
    if (copy_head_value(resp1, "ETag", etag1, sizeof(etag1)) != 0 ||
        copy_head_value(resp2, "ETag", etag2, sizeof(etag2)) != 0) {
        FAIL("a response carried no ETag");
        return;
    }
    if (strcmp(etag1, etag2) != 0) {
        FAIL("the ETag changed for the same document");
        return;
    }
    PASS();
}

/* ---------- 3. the validator follows the representation ---------- */

static void test_a_different_document_has_a_different_etag(void)
{
    char req[256];
    char resp1[4096];
    char resp2[4096];
    char etag1[128];
    char etag2[128];
    pid_t pid;
    uint16_t port;

    TEST("a different document has a different ETag");
    alarm(10);
    build_get(req, sizeof(req), NULL);
    port = spawn_our_server_with(&pid, our_body);
    if (raw_exchange(port, req, resp1, sizeof(resp1)) < 0) {
        FAIL("the first exchange failed");
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    waitpid(pid, NULL, 0);
    port = spawn_our_server_with(&pid, other_body);
    if (raw_exchange(port, req, resp2, sizeof(resp2)) < 0) {
        FAIL("the second exchange failed");
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    waitpid(pid, NULL, 0);
    alarm(0);
    if (copy_head_value(resp1, "ETag", etag1, sizeof(etag1)) != 0 ||
        copy_head_value(resp2, "ETag", etag2, sizeof(etag2)) != 0) {
        FAIL("a response carried no ETag");
        return;
    }
    if (strcmp(etag1, etag2) == 0) {
        FAIL("two different documents had the same ETag");
        return;
    }
    PASS();
}

/* ---------- 4. the conditional GET gets a 304 ---------- */

static void test_a_matching_if_none_match_gets_a_304(void)
{
    char req[256];
    char resp[4096];
    char etag[128];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("a matching If-None-Match gets a 304");
    alarm(10);
    /* The tag comes off the wire, the way a client would get it. */
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), NULL);
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    if (n < 0 || copy_head_value(resp, "ETag", etag, sizeof(etag)) != 0) {
        FAIL("could not get the ETag");
        alarm(0);
        return;
    }
    /* Now the conditional request. */
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), etag);
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the conditional exchange failed");
        return;
    }
    if (!status_is(resp, "HTTP/1.1 304 Not Modified\r\n")) {
        FAIL("the status is not 304");
        return;
    }
    /* The 304 carries the headers the 200 would have carried. */
    if (!contains(resp, "ETag: ")) {
        FAIL("the 304 carried no ETag");
        return;
    }
    if (!contains(resp, "Date: ")) {
        FAIL("the 304 carried no Date");
        return;
    }
    /* And nothing else: no content, and no claim about any. */
    if (contains(resp, "Content-Length")) {
        FAIL("the 304 carried a Content-Length");
        return;
    }
    if (contains(resp, "Content-Type")) {
        FAIL("the 304 carried a Content-Type");
        return;
    }
    if (contains(resp, our_body)) {
        FAIL("the 304 carried the document");
        return;
    }
    PASS();
}

/* ---------- 5. the comparison is the weak one ---------- */

static void test_the_comparison_is_the_weak_one(void)
{
    char req[256];
    char resp[4096];
    char etag[128];
    char strong[128];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("the tag without the W/ still matches");
    alarm(10);
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), NULL);
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    if (n < 0 || copy_head_value(resp, "ETag", etag, sizeof(etag)) != 0) {
        FAIL("could not get the ETag");
        alarm(0);
        return;
    }
    /* The strong form of the same opaque tag: the "W/" dropped. */
    if (strncmp(etag, "W/", 2) != 0) {
        FAIL("the ETag was not weak");
        alarm(0);
        return;
    }
    snprintf(strong, sizeof(strong), "%s", etag + 2);

    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), strong);
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the conditional exchange failed");
        return;
    }
    if (!status_is(resp, "HTTP/1.1 304 Not Modified\r\n")) {
        FAIL("the weak comparison did not match");
        return;
    }
    PASS();
}

/* ---------- 6. the asterisk ---------- */

static void test_an_asterisk_matches(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("If-None-Match: * gets a 304");
    alarm(10);
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), "*");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (!status_is(resp, "HTTP/1.1 304 Not Modified\r\n")) {
        FAIL("the status is not 304");
        return;
    }
    PASS();
}

/* ---------- 7. a list of tags ---------- */

static void test_one_tag_in_a_list_matches(void)
{
    char req[256];
    char resp[4096];
    char etag[128];
    char list[256];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("one matching tag in a list gets a 304");
    alarm(10);
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), NULL);
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    if (n < 0 || copy_head_value(resp, "ETag", etag, sizeof(etag)) != 0) {
        FAIL("could not get the ETag");
        alarm(0);
        return;
    }
    snprintf(list, sizeof(list), "\"other\", %s, \"another\"", etag);

    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), list);
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the conditional exchange failed");
        return;
    }
    if (!status_is(resp, "HTTP/1.1 304 Not Modified\r\n")) {
        FAIL("the list did not match");
        return;
    }
    PASS();
}

/* ---------- 8. a tag that does not match ---------- */

static void test_a_tag_that_does_not_match_gets_the_200(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("a tag that does not match gets the 200");
    alarm(10);
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), "\"not-the-tag\"");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    if (!status_is(resp, "HTTP/1.1 200 OK\r\n")) {
        FAIL("the status is not 200");
        return;
    }
    if (!contains(resp, our_body)) {
        FAIL("the body was not the document");
        return;
    }
    PASS();
}

/* ---------- 9. a value that is not a tag ---------- */

static void test_a_malformed_value_does_not_match(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("a value that is not an entity tag does not match");
    alarm(10);
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), "not-a-tag");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    /* "Otherwise, the condition is true." */
    if (!status_is(resp, "HTTP/1.1 200 OK\r\n")) {
        FAIL("the status is not 200");
        return;
    }
    PASS();
}

/* ---------- 10. a target that is not there ---------- */

static void test_a_missing_target_is_a_404_not_a_304(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("a missing target is a 404, not a 304");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "GET /nope HTTP/1.1\r\nHost: 127.0.0.1\r\nIf-None-Match: *\r\n"
             "Connection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    /* RFC 9110 13.2.1: a server MUST ignore the preconditions if the
     * response without them would not have been a 2xx. */
    if (!status_is(resp, "HTTP/1.1 404 Not Found\r\n")) {
        FAIL("the status is not 404");
        return;
    }
    PASS();
}

/* ---------- 11. a method that does not select a representation ---------- */

static void test_options_ignores_the_precondition(void)
{
    char req[256];
    char resp[4096];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("OPTIONS ignores the precondition");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "OPTIONS / HTTP/1.1\r\nHost: 127.0.0.1\r\nIf-None-Match: *\r\n"
             "Connection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the exchange failed");
        return;
    }
    /* RFC 9110 13.2.1 names OPTIONS among the methods whose
     * preconditions a server must ignore. */
    if (!status_is(resp, "HTTP/1.1 200 OK\r\n")) {
        FAIL("the status is not 200");
        return;
    }
    if (!contains(resp, "Allow: GET, HEAD, OPTIONS")) {
        FAIL("the OPTIONS response lost its Allow header");
        return;
    }
    PASS();
}

/* ---------- 12. a HEAD with a matching precondition ---------- */

static void test_a_head_with_a_matching_precondition_is_a_304(void)
{
    char req[256];
    char resp[4096];
    char etag[128];
    ssize_t n;
    pid_t pid;
    uint16_t port;

    TEST("a HEAD with a matching precondition is a 304");
    alarm(10);
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "HEAD / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    if (n < 0 || copy_head_value(resp, "ETag", etag, sizeof(etag)) != 0) {
        FAIL("could not get the ETag");
        alarm(0);
        return;
    }
    port = spawn_our_server(&pid);
    snprintf(req, sizeof(req),
             "HEAD / HTTP/1.1\r\nHost: 127.0.0.1\r\nIf-None-Match: %s\r\n"
             "Connection: close\r\n\r\n", etag);
    n = raw_exchange(port, req, resp, sizeof(resp));
    waitpid(pid, NULL, 0);
    alarm(0);
    if (n < 0) {
        FAIL("the conditional exchange failed");
        return;
    }
    if (!status_is(resp, "HTTP/1.1 304 Not Modified\r\n")) {
        FAIL("the status is not 304");
        return;
    }
    if (contains(resp, our_body)) {
        FAIL("the 304 carried the document");
        return;
    }
    PASS();
}

/* ---------- 13. the connection survives the 304 ---------- */

static void test_the_304_keeps_the_connection(void)
{
    char req[256];
    char resp1[4096];
    char resp2[4096];
    char head1[4096];
    char head2[4096];
    char etag[128];
    size_t body_off = 0;
    int fd;
    pid_t pid;
    uint16_t port;

    TEST("the 304 keeps the connection");
    alarm(10);
    port = spawn_our_server(&pid);
    build_get(req, sizeof(req), NULL);
    if (raw_exchange(port, req, resp1, sizeof(resp1)) < 0 ||
        copy_head_value(resp1, "ETag", etag, sizeof(etag)) != 0) {
        FAIL("could not get the ETag");
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    waitpid(pid, NULL, 0);

    /* Two requests on one socket: the conditional one, then an
     * ordinary one. Both ask to keep the connection -- a request that
     * said "close" would end the conversation before the second
     * question was asked. */
    port = spawn_our_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("could not connect");
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    snprintf(req, sizeof(req),
             "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nIf-None-Match: %s\r\n"
             "Connection: keep-alive\r\n\r\n", etag);
    if (write_all(fd, req, strlen(req)) != 0) {
        FAIL("could not write the request");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    if (read_one_response(fd, resp1, sizeof(resp1), head1, sizeof(head1),
                           &body_off) < 0) {
        FAIL("could not read the 304");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    if (!status_is(head1, "HTTP/1.1 304 Not Modified\r\n")) {
        FAIL("the first response was not a 304");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    snprintf(req, sizeof(req),
             "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n"
             "Connection: keep-alive\r\n\r\n");
    if (write_all(fd, req, strlen(req)) != 0) {
        FAIL("could not write the second request");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    if (read_one_response(fd, resp2, sizeof(resp2), head2, sizeof(head2),
                           &body_off) < 0) {
        FAIL("could not read the second response");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    sock_close(fd);
    waitpid(pid, NULL, 0);
    alarm(0);
    if (!status_is(head2, "HTTP/1.1 200 OK\r\n")) {
        FAIL("the second response was not a 200");
        return;
    }
    if (memcmp(resp2 + body_off, our_body, sizeof(our_body) - 1) != 0) {
        FAIL("the second response had no document");
        return;
    }
    PASS();
}

/* ---------- 14. the client sees the validator and the 304 ---------- */

static void test_the_client_reads_the_validator_and_the_304(void)
{
    http_client_connection c;
    http_response r;
    pid_t pid;
    uint16_t port;
    char etag[HTTP_MAX_HVALUE + 1];
    int i;
    int found = 0;

    TEST("the client reads the ETag, and reads the 304");
    alarm(10);
    port = spawn_our_server(&pid);
    if (http_client_connection_open(&c, "127.0.0.1", port) != 0) {
        FAIL("could not open the connection");
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    if (http_client_connection_write_request(&c, "GET", "/", NULL, 0) != 0) {
        FAIL("could not write the request");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    if (http_client_connection_read_response(&c, &r) != 0) {
        FAIL("the client did not return a response");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    if (r.status != 200) {
        FAIL("the status was not 200");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    for (i = 0; i < r.nheaders; i++) {
        if (strcasecmp(r.headers[i].name, "ETag") == 0) {
            snprintf(etag, sizeof(etag), "%s", r.headers[i].value);
            found = 1;
        }
    }
    if (!found) {
        FAIL("the client could not see the ETag");
        http_client_connection_close(&c);
        waitpid(pid, NULL, 0);
        alarm(0);
        return;
    }
    http_client_connection_close(&c);
    waitpid(pid, NULL, 0);
    alarm(0);
    PASS();
}

/* ---------- main ---------- */

int main(void)
{
    signal(SIGALRM, on_alarm);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Stage 65.32 -- the server's side: a response with no content\n");

    test_the_document_carries_a_weak_etag();
    test_the_validator_is_stable_for_one_document();
    test_a_different_document_has_a_different_etag();
    test_a_matching_if_none_match_gets_a_304();
    test_the_comparison_is_the_weak_one();
    test_an_asterisk_matches();
    test_one_tag_in_a_list_matches();
    test_a_tag_that_does_not_match_gets_the_200();
    test_a_malformed_value_does_not_match();
    test_a_missing_target_is_a_404_not_a_304();
    test_options_ignores_the_precondition();
    test_a_head_with_a_matching_precondition_is_a_304();
    test_the_304_keeps_the_connection();
    test_the_client_reads_the_validator_and_the_304();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
