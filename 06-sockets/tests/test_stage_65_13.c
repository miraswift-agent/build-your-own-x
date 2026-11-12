/*
 * Stage 65.13 -- the client decodes a chunked response.
 *
 * The oracle is the mirror of Stage 65.12's: the bytes below are a real
 * response from a server that is not ours -- Node's HTTP server, writing
 * a body in three pieces, captured with `curl --raw` so that curl did
 * not decode the chunking for us. The headers are Node's, the framing is
 * Node's, and the body the client hands back must be what the server
 * wrote, byte for byte.
 *
 * The client's third framing: a body can be framed by Content-Length,
 * by the close of the connection, or by chunked transfer-encoding. The
 * client learns which one from the headers -- and when a message carries
 * both Transfer-Encoding and Content-Length, the transfer coding wins,
 * because that shape is the classic request-smuggling trick.
 *
 * The last test is the whole thing over a real socket: a forked child
 * replays Node's bytes, the client fetches, and the body must match.
 */

#include "../src/http_client.h"
#include "../src/sockets.h"

#include <errno.h>
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

/* Node's response, byte for byte: the head (156 bytes) and the chunked
 * body (43 bytes). */
static const unsigned char node_response[] = {
    0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, 0x20, 0x32, 0x30, 0x30,
    0x20, 0x4f, 0x4b, 0x0d, 0x0a, 0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74,
    0x2d, 0x54, 0x79, 0x70, 0x65, 0x3a, 0x20, 0x74, 0x65, 0x78, 0x74, 0x2f,
    0x70, 0x6c, 0x61, 0x69, 0x6e, 0x0d, 0x0a, 0x44, 0x61, 0x74, 0x65, 0x3a,
    0x20, 0x54, 0x68, 0x75, 0x2c, 0x20, 0x31, 0x32, 0x20, 0x4e, 0x6f, 0x76,
    0x20, 0x32, 0x30, 0x32, 0x36, 0x20, 0x31, 0x34, 0x3a, 0x31, 0x34, 0x3a,
    0x35, 0x31, 0x20, 0x47, 0x4d, 0x54, 0x0d, 0x0a, 0x43, 0x6f, 0x6e, 0x6e,
    0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x3a, 0x20, 0x6b, 0x65, 0x65, 0x70,
    0x2d, 0x61, 0x6c, 0x69, 0x76, 0x65, 0x0d, 0x0a, 0x4b, 0x65, 0x65, 0x70,
    0x2d, 0x41, 0x6c, 0x69, 0x76, 0x65, 0x3a, 0x20, 0x74, 0x69, 0x6d, 0x65,
    0x6f, 0x75, 0x74, 0x3d, 0x35, 0x0d, 0x0a, 0x54, 0x72, 0x61, 0x6e, 0x73,
    0x66, 0x65, 0x72, 0x2d, 0x45, 0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e, 0x67,
    0x3a, 0x20, 0x63, 0x68, 0x75, 0x6e, 0x6b, 0x65, 0x64, 0x0d, 0x0a, 0x0d,
    0x0a, 0x37, 0x0d, 0x0a, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c, 0x20, 0x0d,
    0x0a, 0x38, 0x0d, 0x0a, 0x63, 0x68, 0x75, 0x6e, 0x6b, 0x65, 0x64, 0x20,
    0x0d, 0x0a, 0x37, 0x0d, 0x0a, 0x77, 0x6f, 0x72, 0x6c, 0x64, 0x21, 0x0a,
    0x0d, 0x0a, 0x30, 0x0d, 0x0a, 0x0d, 0x0a,
};

#define NODE_RESPONSE_LEN (sizeof(node_response))
#define NODE_HEAD_LEN 156u

static const char expected_body[] = "Hello, chunked world!\n";

static int body_is_right(const http_response *r)
{
    return r->body_len == sizeof(expected_body) - 1 &&
           memcmp(r->body, expected_body, r->body_len) == 0;
}

static void test_the_client_decodes_the_captured_node_response(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("the client decodes the captured Node response");
    http_response_parser_init(&p);
    if (http_response_parser_feed(&p, node_response, NODE_RESPONSE_LEN) != 0) {
        FAIL("feed refused the capture");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the capture did not parse");
        return;
    }
    if (!r.framed_by_chunked) {
        FAIL("the response was not seen as chunked");
        return;
    }
    if (r.content_length != -1) {
        FAIL("a chunked response must not carry a content length");
        return;
    }
    if (!body_is_right(&r)) {
        FAIL("the decoded body is not what Node wrote");
        return;
    }
    PASS();
}

static void test_the_response_may_arrive_in_pieces(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;
    size_t off;

    TEST("the response may arrive in pieces");
    http_response_parser_init(&p);
    http_response_parser_feed(&p, node_response, NODE_HEAD_LEN);
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_NEED_MORE) {
        FAIL("a head without a body should ask for more");
        return;
    }
    for (off = NODE_HEAD_LEN; off < NODE_RESPONSE_LEN; off += 7) {
        size_t n = NODE_RESPONSE_LEN - off;
        if (n > 7) {
            n = 7;
        }
        http_response_parser_feed(&p, node_response + off, n);
        st = http_response_parser_next(&p, &r);
        if (st == HTTP_PARSE_OK) {
            break;
        }
        if (st != HTTP_PARSE_NEED_MORE) {
            FAIL("a partial chunked body should ask for more");
            return;
        }
    }
    if (st != HTTP_PARSE_OK) {
        FAIL("the body never completed");
        return;
    }
    if (!body_is_right(&r)) {
        FAIL("the reassembled body is not what Node wrote");
        return;
    }
    PASS();
}

static void test_content_length_is_ignored_when_the_body_is_chunked(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;
    static const char head[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Content-Length: 999\r\n"
        "\r\n";
    static const unsigned char chunked_body[] = {
        0x37, 0x0d, 0x0a, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c, 0x20,
        0x0d, 0x0a, 0x30, 0x0d, 0x0a, 0x0d, 0x0a
    };

    TEST("Content-Length is ignored when the body is chunked");
    http_response_parser_init(&p);
    http_response_parser_feed(&p, head, strlen(head));
    http_response_parser_feed(&p, chunked_body, sizeof(chunked_body));
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the response did not parse");
        return;
    }
    if (r.content_length != -1) {
        FAIL("the Content-Length was not ignored");
        return;
    }
    if (!r.framed_by_chunked) {
        FAIL("the response was not seen as chunked");
        return;
    }
    if (r.body_len != 7 || memcmp(r.body, "Hello, ", 7) != 0) {
        FAIL("the body is not the chunked one");
        return;
    }
    PASS();
}

static void test_a_chunked_response_cut_short_is_an_error(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("a chunked response cut short is an error");
    http_response_parser_init(&p);
    http_response_parser_feed(&p, node_response, NODE_HEAD_LEN);
    http_response_parser_feed(&p, "7\r\nHel", 7);
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_NEED_MORE) {
        FAIL("a partial chunk should ask for more");
        return;
    }
    /* The peer hung up before the terminal chunk. */
    st = http_response_parser_finish(&p, &r);
    if (st != HTTP_PARSE_ERROR) {
        FAIL("a truncated chunked body should be an error");
        return;
    }
    PASS();
}

static uint16_t spawn_node_server(pid_t *pid_out)
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
            size_t off = 0;
            while (off < NODE_RESPONSE_LEN) {
                ssize_t n = write(fd, node_response + off, NODE_RESPONSE_LEN - off);
                if (n <= 0) {
                    break;
                }
                off += (size_t)n;
            }
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

static void test_the_client_fetches_a_chunked_response_over_a_socket(void)
{
    pid_t pid;
    uint16_t port;
    http_response r;

    TEST("the client fetches a chunked response over a socket");
    port = spawn_node_server(&pid);
    if (http_client_get("127.0.0.1", port, "/", &r) != 0) {
        FAIL("the fetch failed");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    if (r.status != 200) {
        FAIL("the status is not 200");
        return;
    }
    if (!r.framed_by_chunked) {
        FAIL("the response was not seen as chunked");
        return;
    }
    if (!body_is_right(&r)) {
        FAIL("the fetched body is not what Node wrote");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.13 -- the client decodes a chunked response\n\n");

    test_the_client_decodes_the_captured_node_response();
    test_the_response_may_arrive_in_pieces();
    test_content_length_is_ignored_when_the_body_is_chunked();
    test_a_chunked_response_cut_short_is_an_error();
    test_the_client_fetches_a_chunked_response_over_a_socket();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
