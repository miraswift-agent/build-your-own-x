/*
 * Stage 65.5 tests: non-blocking + multiplexing + framing.
 *
 * The lesson: a stream has no message boundaries -- you must frame it
 * yourself.
 *
 * Stage 65.3's server forked one child per connection, which made it
 * concurrent but not multiplexed: one process per connection, and the
 * parent blocking in accept(). A server that blocks owns the process --
 * it cannot look at another connection until the one in front of it
 * finishes. Stage 65.5 makes one process serve many connections by never
 * blocking: every socket is non-blocking, one select() says which
 * descriptors are ready, and each connection keeps its own buffers.
 *
 * And the buffers are not a convenience. The kernel gives you bytes, not
 * packets: one read() can return a third of a message, one message, or
 * three messages and a fragment. The framing layer is what decides
 * which, and nothing in the kernel will do it for you. These tests check
 * the framing as carefully as the multiplexing, because the framing is
 * the part that is easy to get wrong and hard to notice.
 *
 * Hermetic: loopback only, one process, no fork.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../src/sockets.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-56s", #name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("✓\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        printf("✗ %s\n", (msg)); \
        return; \
    } while (0)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            FAIL(msg); \
        } \
    } while (0)

/* The multiplexer under test. Static because it carries a buffer per
 * connection and does not want to live on the stack. */
static sock_mux mux;

/* What the handler saw. */
typedef struct {
    int      calls;
    uint32_t last_len;
    char     last[256];
} hctx;

static int echo_handler(sock_conn *c, const char *msg, uint32_t len, void *ctx)
{
    hctx *h = (hctx *)ctx;

    h->calls++;
    h->last_len = len;
    if (len < sizeof(h->last)) {
        memcpy(h->last, msg, len);
    }
    /* Reply, framed the same way the request was framed. */
    sock_conn_queue_frame(c, msg, len);
    return 0;
}

/* Poll until the handler has been called want times, or we give up. */
static void pump(hctx *h, int want, int max_polls)
{
    int i;

    for (i = 0; i < max_polls && h->calls < want; i++) {
        sock_mux_poll(&mux, echo_handler, h, 20);
    }
}

/* Poll until the server has accepted want connections. */
static void pump_accept(int want, int max_polls)
{
    int i;

    for (i = 0; i < max_polls && mux.nconns < want; i++) {
        sock_mux_poll(&mux, echo_handler, NULL, 20);
    }
}

static int client_write(int fd, const void *buf, size_t len)
{
    ssize_t n = write(fd, buf, len);

    return (n == (ssize_t)len) ? 0 : -1;
}

static int client_send_frame(int fd, const void *payload, uint32_t len)
{
    char buf[SOCK_FRAME_BUF];
    int n = sock_frame_encode(buf, sizeof buf, payload, len);

    if (n < 0) {
        return -1;
    }
    return client_write(fd, buf, (size_t)n);
}

/* Read one framed message from the client side. */
static int client_read_frame(int fd, char *out, size_t out_len, uint32_t *len)
{
    sock_io_result r;
    uint8_t hdr[4];
    uint32_t plen;

    if (sock_read_exact(fd, hdr, 4, &r) != 0 || r.status != SOCK_IO_COMPLETE) {
        return -1;
    }
    plen = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
           ((uint32_t)hdr[2] << 8) | (uint32_t)hdr[3];
    if (plen > out_len) {
        return -1;
    }
    if (sock_read_exact(fd, out, plen, &r) != 0 || r.status != SOCK_IO_COMPLETE) {
        return -1;
    }
    *len = plen;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The framing layer.                                                  */
/* ------------------------------------------------------------------ */

static void test_frame_needs_four_header_bytes(void)
{
    sock_frame f;
    const char *msg = NULL;
    uint32_t len = 0;

    TEST("frame_needs_four_header_bytes");
    sock_frame_init(&f);
    ASSERT(sock_frame_feed(&f, "\x00\x00", 2) == 0, "feed failed");
    ASSERT(sock_frame_next(&f, &msg, &len) == 0,
           "two bytes are not a message -- a header is four bytes");
    PASS();
}

static void test_frame_assembles_across_two_feeds(void)
{
    sock_frame f;
    const char *msg = NULL;
    uint32_t len = 0;

    TEST("frame_assembles_across_two_feeds");
    sock_frame_init(&f);
    ASSERT(sock_frame_feed(&f, "\x00\x00", 2) == 0, "feed 1");
    ASSERT(sock_frame_next(&f, &msg, &len) == 0, "no message yet");
    ASSERT(sock_frame_feed(&f, "\x00\x05hello", 7) == 0, "feed 2");
    ASSERT(sock_frame_next(&f, &msg, &len) == 1, "the message is complete now");
    ASSERT(len == 5, "wrong length");
    ASSERT(memcmp(msg, "hello", 5) == 0, "wrong payload");
    PASS();
}

static void test_frame_yields_three_messages_from_one_feed(void)
{
    sock_frame f;
    const char *msg = NULL;
    uint32_t len = 0;
    char buf[64];
    int n = 0;

    TEST("frame_yields_three_messages_from_one_feed");
    sock_frame_init(&f);
    n += sock_frame_encode(buf + n, sizeof buf - (size_t)n, "one", 3);
    n += sock_frame_encode(buf + n, sizeof buf - (size_t)n, "two", 3);
    n += sock_frame_encode(buf + n, sizeof buf - (size_t)n, "three", 5);
    ASSERT(sock_frame_feed(&f, buf, (size_t)n) == 0, "feed failed");
    ASSERT(sock_frame_next(&f, &msg, &len) == 1 && len == 3 &&
           memcmp(msg, "one", 3) == 0, "first message");
    ASSERT(sock_frame_next(&f, &msg, &len) == 1 && len == 3 &&
           memcmp(msg, "two", 3) == 0, "second message");
    ASSERT(sock_frame_next(&f, &msg, &len) == 1 && len == 5 &&
           memcmp(msg, "three", 5) == 0, "third message");
    ASSERT(sock_frame_next(&f, &msg, &len) == 0, "there is no fourth message");
    PASS();
}

static void test_frame_rejects_oversized_length(void)
{
    sock_frame f;
    const char *msg = NULL;
    uint32_t len = 0;

    TEST("frame_rejects_oversized_length");
    sock_frame_init(&f);
    ASSERT(sock_frame_feed(&f, "\xff\xff\xff\xff", 4) == 0, "feed failed");
    errno = 0;
    ASSERT(sock_frame_next(&f, &msg, &len) == -1,
           "a 4GB length prefix must be refused, not waited for");
    ASSERT(errno == EMSGSIZE, "errno should be EMSGSIZE");
    PASS();
}

static void test_frame_accepts_max_payload(void)
{
    sock_frame f;
    const char *msg = NULL;
    uint32_t len = 0;
    char payload[SOCK_FRAME_MAX];
    char buf[SOCK_FRAME_BUF];
    int n;

    TEST("frame_accepts_max_payload");
    memset(payload, 'x', sizeof payload);
    sock_frame_init(&f);
    n = sock_frame_encode(buf, sizeof buf, payload, SOCK_FRAME_MAX);
    ASSERT(n == 4 + SOCK_FRAME_MAX, "encoded length");
    ASSERT(sock_frame_feed(&f, buf, (size_t)n) == 0, "feed failed");
    ASSERT(sock_frame_next(&f, &msg, &len) == 1, "a max-size message is legal");
    ASSERT(len == SOCK_FRAME_MAX, "wrong length");
    PASS();
}

static void test_frame_rejects_one_over_max(void)
{
    sock_frame f;
    const char *msg = NULL;
    uint32_t len = 0;
    uint8_t hdr[4];

    TEST("frame_rejects_one_over_max");
    hdr[0] = 0x00;
    hdr[1] = 0x00;
    hdr[2] = 0x10;
    hdr[3] = 0x01;  /* 4097 -- one byte over the limit */
    sock_frame_init(&f);
    ASSERT(sock_frame_feed(&f, hdr, 4) == 0, "feed failed");
    ASSERT(sock_frame_next(&f, &msg, &len) == -1, "4097 is over the limit");
    PASS();
}

static void test_frame_zero_length_message(void)
{
    sock_frame f;
    const char *msg = NULL;
    uint32_t len = 0;

    TEST("frame_zero_length_message");
    sock_frame_init(&f);
    ASSERT(sock_frame_feed(&f, "\x00\x00\x00\x00", 4) == 0, "feed failed");
    ASSERT(sock_frame_next(&f, &msg, &len) == 1,
           "a zero-length message is still a message -- not an absence");
    ASSERT(len == 0, "length should be 0");
    PASS();
}

static void test_frame_encode_is_big_endian(void)
{
    char buf[16];
    int n;

    TEST("frame_encode_is_big_endian");
    n = sock_frame_encode(buf, sizeof buf, "ab", 2);
    ASSERT(n == 6, "encoded length");
    ASSERT((unsigned char)buf[0] == 0x00 && (unsigned char)buf[1] == 0x00 &&
           (unsigned char)buf[2] == 0x00 && (unsigned char)buf[3] == 0x02,
           "the length is four bytes, high byte first");
    ASSERT(memcmp(buf + 4, "ab", 2) == 0, "payload follows the header");
    PASS();
}

/* ------------------------------------------------------------------ */
/* Non-blocking.                                                       */
/* ------------------------------------------------------------------ */

static void test_nonblocking_accept_is_eagain_when_empty(void)
{
    int fd;

    TEST("nonblocking_accept_is_eagain_when_empty");
    ASSERT(sock_mux_start(&mux, 0, 8) == 0, "mux start");
    errno = 0;
    fd = sock_accept(&mux.listener);
    ASSERT(fd < 0, "accept with nothing pending must not succeed");
    ASSERT(errno == EAGAIN || errno == EWOULDBLOCK,
           "errno should be EAGAIN -- the queue is empty, not broken");
    sock_close(mux.listener.listen_fd);
    PASS();
}

static void test_nonblocking_read_is_would_block(void)
{
    int sv[2];
    sock_io_result r;
    char buf[8];

    TEST("nonblocking_read_is_would_block");
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    ASSERT(sock_set_nonblocking(sv[0], 1) == 0, "set non-blocking");
    sock_read_some(sv[0], buf, sizeof buf, &r);
    ASSERT(r.status == SOCK_IO_WOULD_BLOCK,
           "an empty non-blocking socket is would-block, not an error");
    ASSERT(r.err == 0,
           "err must be 0 for would-block -- the stage 65.4 rule still holds");
    sock_close(sv[0]);
    sock_close(sv[1]);
    PASS();
}

/* ------------------------------------------------------------------ */
/* The multiplexer.                                                    */
/* ------------------------------------------------------------------ */

static void test_mux_accepts_and_serves_a_client(void)
{
    hctx h;
    int fd;
    char out[64];
    uint32_t olen = 0;

    TEST("mux_accepts_and_serves_a_client");
    memset(&h, 0, sizeof h);
    ASSERT(sock_mux_start(&mux, 0, 8) == 0, "mux start");
    fd = sock_connect_loopback(mux.listener.port);
    ASSERT(fd >= 0, "client connect");
    pump_accept(1, 20);
    ASSERT(mux.nconns == 1, "the server should have accepted one connection");
    ASSERT(mux.accepted == 1, "accepted count");
    ASSERT(client_send_frame(fd, "hello", 5) == 0, "client send");
    pump(&h, 1, 20);
    ASSERT(h.calls == 1, "the handler should have been called once");
    ASSERT(h.last_len == 5 && memcmp(h.last, "hello", 5) == 0, "wrong message");
    ASSERT(client_read_frame(fd, out, sizeof out, &olen) == 0, "client read");
    ASSERT(olen == 5 && memcmp(out, "hello", 5) == 0, "wrong reply");
    sock_close(fd);
    sock_close(mux.listener.listen_fd);
    PASS();
}

static void test_mux_serves_two_clients(void)
{
    hctx h;
    int fd1;
    int fd2;
    char out[64];
    uint32_t olen = 0;

    TEST("mux_serves_two_clients");
    memset(&h, 0, sizeof h);
    ASSERT(sock_mux_start(&mux, 0, 8) == 0, "mux start");
    fd1 = sock_connect_loopback(mux.listener.port);
    fd2 = sock_connect_loopback(mux.listener.port);
    ASSERT(fd1 >= 0 && fd2 >= 0, "client connects");
    pump_accept(2, 20);
    ASSERT(mux.nconns == 2, "both connections accepted");
    ASSERT(client_send_frame(fd1, "a", 1) == 0, "client 1 send");
    ASSERT(client_send_frame(fd2, "b", 1) == 0, "client 2 send");
    pump(&h, 2, 20);
    ASSERT(h.calls == 2, "both messages delivered");
    ASSERT(client_read_frame(fd1, out, sizeof out, &olen) == 0, "client 1 read");
    ASSERT(olen == 1 && out[0] == 'a', "client 1 got its own reply");
    ASSERT(client_read_frame(fd2, out, sizeof out, &olen) == 0, "client 2 read");
    ASSERT(olen == 1 && out[0] == 'b', "client 2 got its own reply");
    sock_close(fd1);
    sock_close(fd2);
    sock_close(mux.listener.listen_fd);
    PASS();
}

static void test_mux_one_idle_client_does_not_block_another(void)
{
    hctx h;
    int idle;
    int busy;
    char out[64];
    uint32_t olen = 0;

    TEST("mux_one_idle_client_does_not_block_another");
    memset(&h, 0, sizeof h);
    ASSERT(sock_mux_start(&mux, 0, 8) == 0, "mux start");
    idle = sock_connect_loopback(mux.listener.port);
    busy = sock_connect_loopback(mux.listener.port);
    ASSERT(idle >= 0 && busy >= 0, "client connects");
    pump_accept(2, 20);
    ASSERT(mux.nconns == 2, "both connections accepted");

    /* The idle client says nothing, ever. It holds its connection open. */
    ASSERT(client_send_frame(busy, "b", 1) == 0, "busy client send");
    pump(&h, 1, 20);
    ASSERT(h.calls == 1, "the busy client was served");
    ASSERT(client_read_frame(busy, out, sizeof out, &olen) == 0,
           "the reply arrives while the other client sits silent");
    ASSERT(mux.nconns == 2, "the idle connection is still open");
    sock_close(idle);
    sock_close(busy);
    sock_close(mux.listener.listen_fd);
    PASS();
}

static void test_mux_reassembles_a_message_split_across_reads(void)
{
    hctx h;
    int fd;
    char buf[SOCK_FRAME_BUF];
    int n;

    TEST("mux_reassembles_a_message_split_across_reads");
    memset(&h, 0, sizeof h);
    ASSERT(sock_mux_start(&mux, 0, 8) == 0, "mux start");
    fd = sock_connect_loopback(mux.listener.port);
    ASSERT(fd >= 0, "client connect");
    pump_accept(1, 20);

    n = sock_frame_encode(buf, sizeof buf, "split", 5);
    ASSERT(client_write(fd, buf, 2) == 0, "half the header");
    pump(&h, 1, 3);
    ASSERT(h.calls == 0, "half a header is not a message");
    ASSERT(client_write(fd, buf + 2, (size_t)n - 2) == 0, "the rest");
    pump(&h, 1, 20);
    ASSERT(h.calls == 1, "the message arrived once it was complete");
    ASSERT(h.last_len == 5 && memcmp(h.last, "split", 5) == 0, "right payload");
    sock_close(fd);
    sock_close(mux.listener.listen_fd);
    PASS();
}

static void test_mux_handles_two_messages_in_one_write(void)
{
    hctx h;
    int fd;
    char buf[2 * SOCK_FRAME_BUF];
    int n = 0;

    TEST("mux_handles_two_messages_in_one_write");
    memset(&h, 0, sizeof h);
    ASSERT(sock_mux_start(&mux, 0, 8) == 0, "mux start");
    fd = sock_connect_loopback(mux.listener.port);
    ASSERT(fd >= 0, "client connect");
    pump_accept(1, 20);

    n += sock_frame_encode(buf + n, sizeof buf - (size_t)n, "one", 3);
    n += sock_frame_encode(buf + n, sizeof buf - (size_t)n, "two", 3);
    ASSERT(client_write(fd, buf, (size_t)n) == 0, "client write");
    pump(&h, 2, 20);
    ASSERT(h.calls == 2, "both messages came out of one read");
    sock_close(fd);
    sock_close(mux.listener.listen_fd);
    PASS();
}

static void test_mux_closes_connection_on_eof(void)
{
    hctx h;
    int fd;
    int i;

    TEST("mux_closes_connection_on_eof");
    memset(&h, 0, sizeof h);
    ASSERT(sock_mux_start(&mux, 0, 8) == 0, "mux start");
    fd = sock_connect_loopback(mux.listener.port);
    ASSERT(fd >= 0, "client connect");
    pump_accept(1, 20);
    ASSERT(mux.nconns == 1, "accepted");
    sock_close(fd);
    for (i = 0; i < 20 && mux.nconns > 0; i++) {
        sock_mux_poll(&mux, echo_handler, &h, 20);
    }
    ASSERT(mux.nconns == 0, "the connection is released after EOF");
    sock_close(mux.listener.listen_fd);
    PASS();
}

static void test_mux_closes_connection_on_oversized_frame(void)
{
    hctx h;
    int fd;
    int i;

    TEST("mux_closes_connection_on_oversized_frame");
    memset(&h, 0, sizeof h);
    ASSERT(sock_mux_start(&mux, 0, 8) == 0, "mux start");
    fd = sock_connect_loopback(mux.listener.port);
    ASSERT(fd >= 0, "client connect");
    pump_accept(1, 20);
    ASSERT(client_write(fd, "\xff\xff\xff\xff", 4) == 0, "client write");
    for (i = 0; i < 20 && mux.nconns > 0; i++) {
        sock_mux_poll(&mux, echo_handler, &h, 20);
    }
    ASSERT(mux.nconns == 0,
           "a length prefix we do not accept must close the connection");
    sock_close(fd);
    sock_close(mux.listener.listen_fd);
    PASS();
}

int main(void)
{
    printf("Stage 65.5 — non-blocking + multiplexing + framing\n\n");

    test_frame_needs_four_header_bytes();
    test_frame_assembles_across_two_feeds();
    test_frame_yields_three_messages_from_one_feed();
    test_frame_rejects_oversized_length();
    test_frame_accepts_max_payload();
    test_frame_rejects_one_over_max();
    test_frame_zero_length_message();
    test_frame_encode_is_big_endian();

    test_nonblocking_accept_is_eagain_when_empty();
    test_nonblocking_read_is_would_block();

    test_mux_accepts_and_serves_a_client();
    test_mux_serves_two_clients();
    test_mux_one_idle_client_does_not_block_another();
    test_mux_reassembles_a_message_split_across_reads();
    test_mux_handles_two_messages_in_one_write();
    test_mux_closes_connection_on_eof();
    test_mux_closes_connection_on_oversized_frame();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("  (%d FAILED)\n", tests_failed);
        return 1;
    }
    printf("\n");
    return 0;
}
