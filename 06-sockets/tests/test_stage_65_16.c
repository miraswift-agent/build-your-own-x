/*
 * Stage 65.16 -- the server routes by Host.
 *
 * The Host header is already on the wire: our client sends it, Node
 * sends it, curl sends it. Our server ignored it. That is not a detail
 * -- a server with one document does not need it, but a server with
 * several does: the Host names the site the request is for.
 *
 * The claim under test:
 *
 *   the Host header selects the document, and the rules of a host name
 *   are the rules of a host name:
 *
 *     - the port in "example.com:8080" is NOT part of the name;
 *     - a host name is case-insensitive;
 *     - an unknown host gets the server's default document;
 *     - a request with NO host at all cannot be routed, and a routing
 *       server says so.
 *
 * The requests here are written as raw bytes on purpose: the claim is
 * about what arrives on the wire, and a client that builds the header
 * for us would be us agreeing with ourselves.
 */

#include "../src/http.h"
#include "../src/sockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

static const http_host_route routes[] = {
    { "alpha.example", "alpha's document\n" },
    { "beta.example",  "beta's document\n"  }
};

#define ROUTE_COUNT (sizeof(routes) / sizeof(routes[0]))
static const char default_document[] = "the default document\n";

static uint16_t spawn_routed_server(pid_t *pid_out)
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
            http_serve_connection_routed(fd, routes, ROUTE_COUNT,
                                         default_document, &st);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n <= 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* Read until the peer closes. Every request in this file asks to
 * close, so the end of the conversation is the end of the file. */
static size_t read_to_eof(int fd, char *buf, size_t cap)
{
    size_t seen = 0;

    while (seen + 1 < cap) {
        ssize_t got = read(fd, buf + seen, cap - seen - 1);
        if (got <= 0) {
            break;
        }
        seen += (size_t)got;
    }
    buf[seen] = '\0';
    return seen;
}

/* Send one request, read the answer, and hand back the response bytes. */
static int ask(uint16_t port, const char *request, char *resp, size_t cap)
{
    int fd = sock_connect_loopback(port);

    if (fd < 0) {
        return -1;
    }
    if (write_all(fd, request, strlen(request)) != 0) {
        sock_close(fd);
        return -1;
    }
    (void)read_to_eof(fd, resp, cap);
    sock_close(fd);
    return 0;
}

static void test_a_matching_host_gets_that_hosts_document(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];

    TEST("a matching Host gets that host's document");
    port = spawn_routed_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nHost: alpha.example\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "HTTP/1.1 200 OK") == NULL) {
        FAIL("the status line is not 200");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "alpha's document\n") == NULL) {
        FAIL("the body is not alpha's");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "the default document") != NULL) {
        FAIL("the default document was served instead");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_port_is_not_part_of_the_host(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];

    TEST("the port is not part of the host name");
    port = spawn_routed_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nHost: alpha.example:8080\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "alpha's document\n") == NULL) {
        FAIL("the host with a port did not match alpha");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_host_name_is_case_insensitive(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];

    TEST("the host name is case-insensitive");
    port = spawn_routed_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nHost: ALPHA.Example\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "alpha's document\n") == NULL) {
        FAIL("the upper-case host did not match alpha");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_an_unknown_host_gets_the_default_document(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];

    TEST("an unknown Host gets the default document");
    port = spawn_routed_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nHost: nowhere.example\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "the default document\n") == NULL) {
        FAIL("the default document was not served");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_a_request_with_no_host_is_a_bad_request(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];

    TEST("a request with no Host is a bad request");
    port = spawn_routed_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "HTTP/1.1 400 Bad Request") == NULL) {
        FAIL("the status line is not 400");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "the default document") != NULL) {
        FAIL("a document was served to a request with no Host");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_two_hosts_on_one_connection_get_their_own_documents(void)
{
    pid_t pid;
    uint16_t port;
    int fd;
    char resp[8192];
    const char *alpha;
    const char *beta;

    TEST("two hosts on one connection get their own documents");
    port = spawn_routed_server(&pid);
    fd = sock_connect_loopback(port);
    if (fd < 0) {
        FAIL("the connection did not open");
        waitpid(pid, NULL, 0);
        return;
    }
    /* Two requests, one write: the routing is per request, not per
     * connection. */
    if (write_all(fd,
                  "GET / HTTP/1.1\r\nHost: beta.example\r\nConnection: keep-alive\r\n\r\n"
                  "GET / HTTP/1.1\r\nHost: alpha.example\r\nConnection: close\r\n\r\n",
                  strlen("GET / HTTP/1.1\r\nHost: beta.example\r\nConnection: keep-alive\r\n\r\n"
                         "GET / HTTP/1.1\r\nHost: alpha.example\r\nConnection: close\r\n\r\n")) != 0) {
        FAIL("the write failed");
        sock_close(fd);
        waitpid(pid, NULL, 0);
        return;
    }
    (void)read_to_eof(fd, resp, sizeof(resp));
    sock_close(fd);
    waitpid(pid, NULL, 0);

    alpha = strstr(resp, "alpha's document");
    beta = strstr(resp, "beta's document");
    if (alpha == NULL || beta == NULL) {
        FAIL("both documents are not in the responses");
        return;
    }
    if (beta > alpha) {
        FAIL("the responses are not in the order the requests were written");
        return;
    }
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Stage 65.16 -- the server routes by Host\n\n");

    test_a_matching_host_gets_that_hosts_document();
    test_the_port_is_not_part_of_the_host();
    test_the_host_name_is_case_insensitive();
    test_an_unknown_host_gets_the_default_document();
    test_a_request_with_no_host_is_a_bad_request();
    test_two_hosts_on_one_connection_get_their_own_documents();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
