/*
 * Stage 65.14a -- the parser knows where the next message starts.
 *
 * A connection carries messages back to back. A response can end before
 * the bytes that follow it have been read -- the peer has already sent
 * the next request's response, and it arrived in the same read as the
 * end of this one.
 *
 * So the parser must not throw those bytes away. It must keep them, and
 * the next call must find them. This is the primitive that keep-alive
 * needs, and it is the half that is easy to get wrong: a chunked
 * response's framing is consumed by the chunk decoder, so the bytes
 * after the terminal chunk end up inside the decoder rather than in the
 * parser's buffer, and a parser that only looks at its own buffer
 * loses a message the peer has already sent.
 *
 * The oracle is Node's captured response -- the same 199 bytes Stage
 * 65.13 used -- because the question here is not "what does a chunked
 * body mean" (Stage 65.13 answered that) but "what happens to the bytes
 * that come after it".
 */

#include "../src/http_client.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

/* Node's response, byte for byte: a 156-byte head and a 43-byte body. */
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

#define NODE_LEN (sizeof(node_response))

static const char expected_body[] = "Hello, chunked world!\n";

/* A response framed by its length -- the other framing, for the mix. */
static const char length_response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "\r\n"
    "hello";

#define LENGTH_LEN (sizeof(length_response) - 1)

static int body_is_right(const http_response *r)
{
    return r->body_len == sizeof(expected_body) - 1 &&
           memcmp(r->body, expected_body, r->body_len) == 0;
}

static void test_two_chunked_responses_in_one_buffer(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("two chunked responses in one buffer");
    http_response_parser_init(&p);
    http_response_parser_feed(&p, node_response, NODE_LEN);
    http_response_parser_feed(&p, node_response, NODE_LEN);

    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK || !body_is_right(&r)) {
        FAIL("the first response did not parse");
        return;
    }
    /* The second response's bytes are still here -- the peer sent them
     * in the same read. */
    if (http_response_parser_pending(&p) != NODE_LEN) {
        FAIL("the next message's bytes were not kept");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK || !body_is_right(&r)) {
        FAIL("the second response did not parse");
        return;
    }
    if (http_response_parser_pending(&p) != 0) {
        FAIL("bytes are left over after the second response");
        return;
    }
    PASS();
}

static void test_a_chunked_response_followed_by_a_length_framed_one(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("a chunked response followed by a length-framed one");
    http_response_parser_init(&p);
    http_response_parser_feed(&p, node_response, NODE_LEN);
    http_response_parser_feed(&p, length_response, LENGTH_LEN);

    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK || !body_is_right(&r)) {
        FAIL("the chunked response did not parse");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK) {
        FAIL("the length-framed response did not parse");
        return;
    }
    if (r.body_len != 5 || memcmp(r.body, "hello", 5) != 0) {
        FAIL("the second body is wrong");
        return;
    }
    if (r.framed_by_chunked) {
        FAIL("the second response was seen as chunked");
        return;
    }
    PASS();
}

static void test_a_length_framed_response_followed_by_a_chunked_one(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;

    TEST("a length-framed response followed by a chunked one");
    http_response_parser_init(&p);
    http_response_parser_feed(&p, length_response, LENGTH_LEN);
    http_response_parser_feed(&p, node_response, NODE_LEN);

    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK || r.body_len != 5) {
        FAIL("the length-framed response did not parse");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK || !body_is_right(&r)) {
        FAIL("the chunked response did not parse");
        return;
    }
    if (!r.framed_by_chunked) {
        FAIL("the second response was not seen as chunked");
        return;
    }
    PASS();
}

static void test_the_next_message_may_arrive_in_pieces(void)
{
    http_response_parser p;
    http_response r;
    http_parse_status st;
    size_t half;

    TEST("the next message may arrive in pieces");
    http_response_parser_init(&p);
    http_response_parser_feed(&p, node_response, NODE_LEN);
    half = NODE_LEN / 2;
    http_response_parser_feed(&p, node_response, half);

    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK || !body_is_right(&r)) {
        FAIL("the first response did not parse");
        return;
    }
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_NEED_MORE) {
        FAIL("a half-arrived message should ask for more");
        return;
    }
    http_response_parser_feed(&p, node_response + half, NODE_LEN - half);
    st = http_response_parser_next(&p, &r);
    if (st != HTTP_PARSE_OK || !body_is_right(&r)) {
        FAIL("the completed message did not parse");
        return;
    }
    PASS();
}

static void test_the_parser_says_how_much_it_is_holding(void)
{
    http_response_parser p;
    http_response r;

    TEST("the parser says how much it is holding");
    http_response_parser_init(&p);
    if (http_response_parser_pending(&p) != 0) {
        FAIL("a fresh parser holds nothing");
        return;
    }
    http_response_parser_feed(&p, node_response, NODE_LEN);
    if (http_response_parser_pending(&p) != NODE_LEN) {
        FAIL("the parser does not count the bytes it holds");
        return;
    }
    http_response_parser_next(&p, &r);
    if (http_response_parser_pending(&p) != 0) {
        FAIL("the parser should hold nothing after the only message");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.14a -- the parser knows where the next message starts\n\n");

    test_two_chunked_responses_in_one_buffer();
    test_a_chunked_response_followed_by_a_length_framed_one();
    test_a_length_framed_response_followed_by_a_chunked_one();
    test_the_next_message_may_arrive_in_pieces();
    test_the_parser_says_how_much_it_is_holding();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
