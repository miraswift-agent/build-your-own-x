/*
 * Stage 65.11 -- chunked transfer-encoding, the decoder.
 *
 * The oracle is a CAPTURE, not a memory. The bytes in `captured_chunked`
 * came off a real socket: a Node HTTP server (an implementation with
 * nothing to do with this component) wrote a body in pieces, and curl
 * was told `--raw` so that it would not decode the chunking for us. The
 * framing is Node's, not ours.
 *
 *     $ node chunksrv.js &
 *     $ curl -sS --raw -D headers.txt http://127.0.0.1:18099/ -o capture.bin
 *     $ xxd capture.bin
 *     00000000: 370d 0a48 656c 6c6f 2c20 0d0a 380d 0a63  ...
 *
 * The decoder's job is to turn that back into "Hello, chunked world!\n"
 * -- byte for byte, because the whole point of a framing is that the
 * framing is not part of the message.
 *
 * The second half of the lesson is that a chunk can arrive in pieces.
 * "7\r\nHel" is not an error; it is a connection that is still open. The
 * decoder says NEED_MORE and asks for the rest.
 */

#include "../src/http.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

/* The captured response body, chunked, exactly as it came off the wire. */
static const unsigned char captured_chunked[] = {
    0x37, 0x0d, 0x0a,                                     /* 7\r\n */
    0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c, 0x20,             /* Hello,  */
    0x0d, 0x0a,                                           /* \r\n */
    0x38, 0x0d, 0x0a,                                     /* 8\r\n */
    0x63, 0x68, 0x75, 0x6e, 0x6b, 0x65, 0x64, 0x20,       /* chunked */
    0x0d, 0x0a,                                           /* \r\n */
    0x37, 0x0d, 0x0a,                                     /* 7\r\n */
    0x77, 0x6f, 0x72, 0x6c, 0x64, 0x21, 0x0a,             /* world!\n */
    0x0d, 0x0a,                                           /* \r\n */
    0x30, 0x0d, 0x0a,                                     /* 0\r\n */
    0x0d, 0x0a                                            /* \r\n */
};

#define CAPTURED_LEN (sizeof(captured_chunked))

static const char expected_body[] = "Hello, chunked world!\n";

static void test_the_capture_decodes_to_the_body(void)
{
    http_chunk_decoder d;
    char out[64];
    char body[128];
    size_t out_len, body_len = 0;
    int chunks = 0;
    http_chunk_status st;

    TEST("the captured response decodes to the body");
    http_chunk_decoder_init(&d);
    if (http_chunk_decoder_feed(&d, captured_chunked, CAPTURED_LEN) != 0) {
        FAIL("feed refused the capture");
        return;
    }
    for (;;) {
        st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
        if (st == HTTP_CHUNK_OK) {
            chunks++;
            if (body_len + out_len > sizeof(body)) {
                FAIL("body overflowed the buffer");
                return;
            }
            memcpy(body + body_len, out, out_len);
            body_len += out_len;
            continue;
        }
        if (st == HTTP_CHUNK_DONE) {
            break;
        }
        FAIL("the capture did not decode");
        return;
    }
    body[body_len] = '\0';
    if (body_len != sizeof(expected_body) - 1) {
        FAIL("the decoded body has the wrong length");
        return;
    }
    if (memcmp(body, expected_body, body_len) != 0) {
        FAIL("the decoded body is not the captured body");
        return;
    }
    if (chunks != 3) {
        FAIL("the capture is not three chunks");
        return;
    }
    PASS();
}

static void test_each_chunk_arrives_as_its_own_piece(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("each chunk arrives as its own piece");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, captured_chunked, CAPTURED_LEN);
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_OK || out_len != 7) {
        FAIL("the first chunk is not 'Hello, '");
        return;
    }
    if (memcmp(out, "Hello, ", 7) != 0) {
        FAIL("the first chunk is not 'Hello, '");
        return;
    }
    PASS();
}

static void test_a_chunk_split_across_feeds_is_not_an_error(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("a chunk split across feeds is not an error");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "7\r\nHel", strlen("7\r\nHel"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_NEED_MORE) {
        FAIL("a half-arrived chunk should ask for more");
        return;
    }
    http_chunk_decoder_feed(&d, "lo, \r\n", strlen("lo, \r\n"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_OK || out_len != 7) {
        FAIL("the rest of the chunk did not arrive");
        return;
    }
    if (memcmp(out, "Hello, ", 7) != 0) {
        FAIL("the reassembled chunk is wrong");
        return;
    }
    PASS();
}

static void test_an_incomplete_chunk_asks_for_more(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("an incomplete chunk asks for more");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "7\r\nHello, ", strlen("7\r\nHello, "));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_NEED_MORE) {
        FAIL("a chunk without its trailing CRLF should ask for more");
        return;
    }
    PASS();
}

static void test_the_terminal_chunk_ends_the_body(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("the terminal chunk ends the body");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "0\r\n\r\n", strlen("0\r\n\r\n"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_DONE) {
        FAIL("the terminal chunk did not end the body");
        return;
    }
    if (out_len != 0) {
        FAIL("the terminal chunk produced bytes");
        return;
    }
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_DONE) {
        FAIL("a finished body should keep saying done");
        return;
    }
    PASS();
}

static void test_trailers_are_consumed_and_the_body_ends(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("trailers are consumed and the body ends");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "5\r\nhello\r\n0\r\nX-Checksum: abc\r\n\r\n",
                            strlen("5\r\nhello\r\n0\r\nX-Checksum: abc\r\n\r\n"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_OK || out_len != 5) {
        FAIL("the chunk before the trailers did not decode");
        return;
    }
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_DONE) {
        FAIL("the trailers did not end the body");
        return;
    }
    PASS();
}

static void test_chunk_extensions_are_ignored(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("chunk extensions are ignored");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "5;foo=bar\r\nhello\r\n0\r\n\r\n",
                            strlen("5;foo=bar\r\nhello\r\n0\r\n\r\n"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_OK || out_len != 5) {
        FAIL("the chunk with an extension did not decode");
        return;
    }
    if (memcmp(out, "hello", 5) != 0) {
        FAIL("the chunk with an extension decoded wrong");
        return;
    }
    PASS();
}

static void test_a_bad_size_line_is_an_error(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("a bad size line is an error");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "zz\r\n", strlen("zz\r\n"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_ERROR) {
        FAIL("a size that is not hexadecimal should be an error");
        return;
    }
    PASS();
}

static void test_a_size_with_no_digits_is_an_error(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("a size with no digits is an error");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "\r\n", strlen("\r\n"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_ERROR) {
        FAIL("an empty size should be an error");
        return;
    }
    PASS();
}

static void test_a_missing_crlf_after_the_payload_is_an_error(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("a missing CRLF after the payload is an error");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "2\r\nabXX", strlen("2\r\nabXX"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_ERROR) {
        FAIL("a payload without its CRLF should be an error");
        return;
    }
    PASS();
}

static void test_an_empty_buffer_asks_for_more(void)
{
    http_chunk_decoder d;
    char out[64];
    size_t out_len;
    http_chunk_status st;

    TEST("an empty buffer asks for more");
    http_chunk_decoder_init(&d);
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_NEED_MORE) {
        FAIL("nothing fed should ask for more");
        return;
    }
    PASS();
}

static void test_a_chunk_too_big_for_the_output_is_an_error(void)
{
    http_chunk_decoder d;
    char out[4];
    size_t out_len;
    http_chunk_status st;

    TEST("a chunk too big for the output is an error");
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, "5\r\nhello\r\n", strlen("5\r\nhello\r\n"));
    st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
    if (st != HTTP_CHUNK_ERROR) {
        FAIL("a chunk that does not fit should be an error");
        return;
    }
    if (errno != ENOBUFS) {
        FAIL("the error should be ENOBUFS");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.11 -- chunked transfer-encoding, the decoder\n\n");

    test_the_capture_decodes_to_the_body();
    test_each_chunk_arrives_as_its_own_piece();
    test_a_chunk_split_across_feeds_is_not_an_error();
    test_an_incomplete_chunk_asks_for_more();
    test_the_terminal_chunk_ends_the_body();
    test_trailers_are_consumed_and_the_body_ends();
    test_chunk_extensions_are_ignored();
    test_a_bad_size_line_is_an_error();
    test_a_size_with_no_digits_is_an_error();
    test_a_missing_crlf_after_the_payload_is_an_error();
    test_an_empty_buffer_asks_for_more();
    test_a_chunk_too_big_for_the_output_is_an_error();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
