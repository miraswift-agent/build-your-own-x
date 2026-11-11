/*
 * Stage 65.1 tests: the fd lifecycle.
 *
 * The lesson under test: a connection is a kernel object; the process owns
 * only an integer. Every assertion here is about descriptors -- which numbers
 * exist, which numbers are live, which numbers the kernel hands back.
 *
 * Hermetic: loopback only. No test asserts on another host's response.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/sockets.h"

static int tests_run = 0;
static int tests_passed = 0;

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
        printf("✗ %s\n", msg); \
    } while (0)

/* ------------------------------------------------------------------ */
/* 1. socket() hands back an integer that names a live descriptor.      */
/* ------------------------------------------------------------------ */
static void test_socket_returns_a_valid_fd(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    TEST("socket_returns_a_valid_fd");
    if (fd < 0) {
        FAIL("socket() failed");
        return;
    }
    if (fcntl(fd, F_GETFD) == -1) {
        FAIL("fd is not a live descriptor");
        close(fd);
        return;
    }
    close(fd);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 2. Bind to port 0 and the kernel tells us which port it chose.       */
/*    The address is a fact the kernel holds, not one we guessed.       */
/* ------------------------------------------------------------------ */
static void test_bind_resolves_a_real_port(void)
{
    sock_listener l;

    TEST("bind_to_port_zero_resolves_a_real_port");
    if (sock_listen_loopback(&l, 0, 8) != 0) {
        FAIL("sock_listen_loopback failed");
        return;
    }
    if (l.port == 0) {
        FAIL("kernel did not assign a port");
        sock_close(l.listen_fd);
        return;
    }
    sock_close(l.listen_fd);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 3. A connection is its own kernel object: accept() returns a          */
/*    different descriptor than the listener (and than the client).      */
/* ------------------------------------------------------------------ */
static void test_accept_returns_a_distinct_fd(void)
{
    sock_listener l;
    int client = -1;
    int conn = -1;

    TEST("accept_returns_a_distinct_fd");
    if (sock_listen_loopback(&l, 0, 8) != 0) {
        FAIL("sock_listen_loopback failed");
        return;
    }
    client = sock_connect_loopback(l.port);
    if (client < 0) {
        FAIL("sock_connect_loopback failed");
        sock_close(l.listen_fd);
        return;
    }
    conn = sock_accept(&l);
    if (conn < 0) {
        FAIL("sock_accept failed");
        sock_close(client);
        sock_close(l.listen_fd);
        return;
    }
    if (conn == l.listen_fd) {
        FAIL("accept returned the listener's fd");
    } else if (conn == client) {
        FAIL("accept returned the client's fd");
    } else {
        PASS();
    }
    sock_close(conn);
    sock_close(client);
    sock_close(l.listen_fd);
}

/* ------------------------------------------------------------------ */
/* 4. The bytes survive the round trip. Two descriptors, one kernel     */
/*    object, one stream.                                                */
/* ------------------------------------------------------------------ */
static void test_echo_round_trip_preserves_bytes(void)
{
    sock_listener l;
    int client = -1;
    int conn = -1;
    const char *msg = "lantern";
    char buf[64];
    ssize_t n;

    TEST("echo_round_trip_preserves_bytes");
    if (sock_listen_loopback(&l, 0, 8) != 0) {
        FAIL("sock_listen_loopback failed");
        return;
    }
    client = sock_connect_loopback(l.port);
    conn = (client < 0) ? -1 : sock_accept(&l);
    if (client < 0 || conn < 0) {
        FAIL("could not establish a connection");
        if (client >= 0) sock_close(client);
        sock_close(l.listen_fd);
        return;
    }

    n = sock_write_once(client, msg, strlen(msg));
    if (n != (ssize_t)strlen(msg)) {
        FAIL("write did not send the whole message");
        goto done;
    }
    n = sock_read_once(conn, buf, sizeof(buf));
    if (n != (ssize_t)strlen(msg) || memcmp(buf, msg, strlen(msg)) != 0) {
        FAIL("server did not receive the message intact");
        goto done;
    }
    /* echo it back */
    n = sock_write_once(conn, buf, (size_t)n);
    if (n != (ssize_t)strlen(msg)) {
        FAIL("echo write did not send the whole message");
        goto done;
    }
    n = sock_read_once(client, buf, sizeof(buf));
    if (n != (ssize_t)strlen(msg) || memcmp(buf, msg, strlen(msg)) != 0) {
        FAIL("client did not receive the echo intact");
        goto done;
    }
    PASS();

done:
    sock_close(conn);
    sock_close(client);
    sock_close(l.listen_fd);
}

/* ------------------------------------------------------------------ */
/* 5. A closed descriptor is refused: the kernel owns the connection,   */
/*    not the integer.                                                   */
/* ------------------------------------------------------------------ */
static void test_closed_fd_is_refused_with_ebadf(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    char buf[16];
    ssize_t n;

    TEST("closed_fd_is_refused_with_ebadf");
    if (fd < 0) {
        FAIL("socket() failed");
        return;
    }
    sock_close(fd);
    errno = 0;
    n = sock_read_once(fd, buf, sizeof(buf));
    if (n != -1 || errno != EBADF) {
        FAIL("read on a closed fd did not fail with EBADF");
        return;
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/* 6. Closing releases the number. The next socket() gets it back --    */
/*    which is the whole point: we own an integer, not a resource.      */
/* ------------------------------------------------------------------ */
static void test_closed_fd_number_is_reusable(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int number = fd;
    int again;

    TEST("closed_fd_number_is_reusable");
    if (fd < 0) {
        FAIL("socket() failed");
        return;
    }
    sock_close(fd);
    again = socket(AF_INET, SOCK_STREAM, 0);
    if (again < 0) {
        FAIL("second socket() failed");
        return;
    }
    if (again != number) {
        FAIL("the released descriptor number was not handed back");
        close(again);
        return;
    }
    close(again);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 7. A listening descriptor is not a connection. Reading one fails.    */
/* ------------------------------------------------------------------ */
static void test_listening_fd_is_not_a_connection(void)
{
    sock_listener l;
    char buf[16];
    ssize_t n;

    TEST("listening_fd_is_not_a_connection");
    if (sock_listen_loopback(&l, 0, 8) != 0) {
        FAIL("sock_listen_loopback failed");
        return;
    }
    errno = 0;
    n = sock_read_once(l.listen_fd, buf, sizeof(buf));
    if (n != -1) {
        FAIL("read on a listening fd unexpectedly succeeded");
        sock_close(l.listen_fd);
        return;
    }
    if (errno != ENOTCONN && errno != EAGAIN && errno != EINVAL) {
        FAIL("read on a listening fd failed with an unexpected errno");
        sock_close(l.listen_fd);
        return;
    }
    sock_close(l.listen_fd);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 8. Closing twice is an error, not a shrug.                          */
/* ------------------------------------------------------------------ */
static void test_double_close_reports_ebadf(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int r;

    TEST("double_close_reports_ebadf");
    if (fd < 0) {
        FAIL("socket() failed");
        return;
    }
    sock_close(fd);
    errno = 0;
    r = sock_close(fd);
    if (r != -1 || errno != EBADF) {
        FAIL("closing an already-closed fd did not fail with EBADF");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.1 — the fd lifecycle\n\n");

    test_socket_returns_a_valid_fd();
    test_bind_resolves_a_real_port();
    test_accept_returns_a_distinct_fd();
    test_echo_round_trip_preserves_bytes();
    test_closed_fd_is_refused_with_ebadf();
    test_closed_fd_number_is_reusable();
    test_listening_fd_is_not_a_connection();
    test_double_close_reports_ebadf();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
