/*
 * Stage 65.12 -- chunked transfer-encoding, the encoder.
 *
 * The oracle here is the CAPTURE FROM STAGE 65.11, and it is the
 * strongest kind of oracle there is: not "our decoder agrees with our
 * encoder", but "our encoder produces the very bytes an independent
 * implementation produced".
 *
 * The bytes below came off a real socket, from Node's HTTP server,
 * which wrote its body in three pieces. Node's framing of those three
 * writes was:
 *
 *     7\r\nHello, \r\n8\r\nchunked \r\n7\r\nworld!\n\r\n0\r\n\r\n
 *
 * Our encoder is handed the same three pieces and must produce the
 * same 43 bytes, byte for byte -- because the framing is not a
 * matter of taste. The last test in this file goes the other way: the
 * bytes are handed to the Stage 65.11 decoder and the body comes back
 * out.
 */

#include "../src/http.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

/* What Node's server put on the wire: three chunks and the terminal
 * chunk, 43 bytes. */
static const unsigned char captured_chunked[] = {
    0x37, 0x0d, 0x0a,
    0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c, 0x20,
    0x0d, 0x0a,
    0x38, 0x0d, 0x0a,
    0x63, 0x68, 0x75, 0x6e, 0x6b, 0x65, 0x64, 0x20,
    0x0d, 0x0a,
    0x37, 0x0d, 0x0a,
    0x77, 0x6f, 0x72, 0x6c, 0x64, 0x21, 0x0a,
    0x0d, 0x0a,
    0x30, 0x0d, 0x0a,
    0x0d, 0x0a
};

#define CAPTURED_LEN (sizeof(captured_chunked))

/* The same body, in the three pieces Node wrote it in. */
static const char *const pieces[] = { "Hello, ", "chunked ", "world!\n" };
#define PIECES 3

static void test_a_chunk_is_framed_as_size_payload_crlf(void)
{
    char out[64];
    int n;

    TEST("a chunk is framed as size, payload, CRLF");
    n = http_chunk_encode("hello", 5, out, sizeof(out));
    if (n != 10) {
        FAIL("the framed chunk is not 10 bytes");
        return;
    }
    if (memcmp(out, "5\r\nhello\r\n", 10) != 0) {
        FAIL("the framed chunk is not '5\\r\\nhello\\r\\n'");
        return;
    }
    PASS();
}

static void test_the_encoder_reproduces_the_captured_framing(void)
{
    char out[128];
    size_t at = 0;
    int i;

    TEST("the encoder reproduces the captured framing");
    for (i = 0; i < PIECES; i++) {
        int n = http_chunk_encode(pieces[i], strlen(pieces[i]), out + at,
                                  sizeof(out) - at);
        if (n < 0) {
            FAIL("the encoder refused a chunk");
            return;
        }
        at += (size_t)n;
    }
    {
        int n = http_chunk_encoder_finish(out + at, sizeof(out) - at);
        if (n < 0) {
            FAIL("the encoder refused the terminal chunk");
            return;
        }
        at += (size_t)n;
    }
    if (at != CAPTURED_LEN) {
        FAIL("the framed body is not the captured length");
        return;
    }
    if (memcmp(out, captured_chunked, CAPTURED_LEN) != 0) {
        FAIL("the framed body is not the captured bytes");
        return;
    }
    PASS();
}

static void test_the_round_trip_returns_the_body(void)
{
    http_chunk_decoder d;
    char out[128];
    char body[128];
    size_t at = 0, body_len = 0, out_len;
    http_chunk_status st;
    int i;

    TEST("the round trip returns the body");
    for (i = 0; i < PIECES; i++) {
        int n = http_chunk_encode(pieces[i], strlen(pieces[i]), out + at,
                                  sizeof(out) - at);
        if (n < 0) {
            FAIL("the encoder refused a chunk");
            return;
        }
        at += (size_t)n;
    }
    {
        int n = http_chunk_encoder_finish(out + at, sizeof(out) - at);
        if (n < 0) {
            FAIL("the encoder refused the terminal chunk");
            return;
        }
        at += (size_t)n;
    }
    http_chunk_decoder_init(&d);
    http_chunk_decoder_feed(&d, out, at);
    for (;;) {
        st = http_chunk_decoder_next(&d, out, sizeof(out), &out_len);
        if (st == HTTP_CHUNK_OK) {
            memcpy(body + body_len, out, out_len);
            body_len += out_len;
            continue;
        }
        if (st == HTTP_CHUNK_DONE) {
            break;
        }
        FAIL("our own bytes did not decode");
        return;
    }
    body[body_len] = '\0';
    if (strcmp(body, "Hello, chunked world!\n") != 0) {
        FAIL("the round trip did not return the body");
        return;
    }
    PASS();
}

static void test_the_terminal_chunk_is_the_last_two_crlf(void)
{
    char out[8];
    int n;

    TEST("the terminal chunk is the last two CRLF");
    n = http_chunk_encoder_finish(out, sizeof(out));
    if (n != 5) {
        FAIL("the terminal chunk is not 5 bytes");
        return;
    }
    if (memcmp(out, "0\r\n\r\n", 5) != 0) {
        FAIL("the terminal chunk is not '0\\r\\n\\r\\n'");
        return;
    }
    PASS();
}

static void test_a_zero_length_chunk_is_refused(void)
{
    char out[16];
    int n;

    TEST("a zero-length chunk is refused");
    n = http_chunk_encode("", 0, out, sizeof(out));
    if (n != -1 || errno != EINVAL) {
        FAIL("a zero-length chunk should be refused");
        return;
    }
    PASS();
}

static void test_a_chunk_too_big_for_the_buffer_is_refused(void)
{
    char out[8];
    int n;

    TEST("a chunk too big for the buffer is refused");
    n = http_chunk_encode("hello", 5, out, sizeof(out));
    if (n != -1 || errno != ENOSPC) {
        FAIL("a chunk that does not fit should be refused");
        return;
    }
    PASS();
}

static void test_the_chunked_header_carries_no_content_length(void)
{
    char hdr[256];
    int n;

    TEST("the chunked header carries no Content-Length");
    n = http_response_build_chunked(hdr, sizeof(hdr), 200, "OK", "text/plain", 1);
    if (n < 0) {
        FAIL("the header block was refused");
        return;
    }
    if (strstr(hdr, "Transfer-Encoding: chunked") == NULL) {
        FAIL("the header block does not say chunked");
        return;
    }
    if (strstr(hdr, "Content-Length") != NULL) {
        FAIL("the header block still carries a Content-Length");
        return;
    }
    if (strstr(hdr, "HTTP/1.1 200 OK") == NULL) {
        FAIL("the header block has no status line");
        return;
    }
    if (strstr(hdr, "Connection: keep-alive") == NULL) {
        FAIL("the header block has no Connection header");
        return;
    }
    PASS();
}

static void test_the_chunked_header_ends_with_a_blank_line(void)
{
    char hdr[256];
    int n;

    TEST("the chunked header ends with a blank line");
    n = http_response_build_chunked(hdr, sizeof(hdr), 200, "OK", "text/plain", 0);
    if (n < 4) {
        FAIL("the header block is too short");
        return;
    }
    if (memcmp(hdr + n - 4, "\r\n\r\n", 4) != 0) {
        FAIL("the header block does not end with a blank line");
        return;
    }
    PASS();
}


static void test_the_server_answers_with_a_chunked_body(void)
{
    int sv[2];
    char req[] = "GET /stream HTTP/1.1\r\nHost: localhost\r\n"
                 "Connection: close\r\n\r\n";
    char resp[2048];
    size_t total = 0;
    ssize_t n;
    http_server_stats st;
    char *body;

    TEST("the server answers with a chunked body");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        FAIL("socketpair");
        return;
    }
    if (write(sv[1], req, sizeof(req) - 1) != (ssize_t)(sizeof(req) - 1)) {
        FAIL("could not write the request");
        return;
    }
    shutdown(sv[1], SHUT_WR);
    http_serve_connection(sv[0], "root\n", &st);
    close(sv[0]);
    for (;;) {
        n = read(sv[1], resp + total, sizeof(resp) - total - 1);
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
    }
    close(sv[1]);
    resp[total] = '\0';
    if (strstr(resp, "Transfer-Encoding: chunked") == NULL) {
        FAIL("the response does not say chunked");
        return;
    }
    if (strstr(resp, "Content-Length") != NULL) {
        FAIL("the response still carries a Content-Length");
        return;
    }
    body = strstr(resp, "\r\n\r\n");
    if (body == NULL) {
        FAIL("the response has no header/body boundary");
        return;
    }
    body += 4;
    if (strlen(body) != CAPTURED_LEN) {
        FAIL("the body is not the captured length");
        return;
    }
    if (memcmp(body, captured_chunked, CAPTURED_LEN) != 0) {
        FAIL("the body on the wire is not the captured bytes");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.12 -- chunked transfer-encoding, the encoder\n\n");

    test_a_chunk_is_framed_as_size_payload_crlf();
    test_the_encoder_reproduces_the_captured_framing();
    test_the_round_trip_returns_the_body();
    test_the_terminal_chunk_is_the_last_two_crlf();
    test_a_zero_length_chunk_is_refused();
    test_a_chunk_too_big_for_the_buffer_is_refused();
    test_the_chunked_header_carries_no_content_length();
    test_the_chunked_header_ends_with_a_blank_line();
    test_the_server_answers_with_a_chunked_body();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
