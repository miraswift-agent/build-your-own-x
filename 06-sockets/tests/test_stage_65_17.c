/*
 * Stage 65.17 -- the server sends Date:.
 *
 * RFC 9110 6.6.1: an origin server MUST generate a Date header field
 * in all 2xx, 3xx, 4xx, or 5xx responses, unless the server does not
 * have a clock. Ours has a clock, and ours sends no Date at all.
 *
 * The claim under test:
 *
 *   every response this server sends carries a Date, and the value is
 *   the moment the response was made, in the format a cache can parse:
 *
 *     - IMF-fixdate: "Sun, 06 Nov 1994 08:49:37 GMT";
 *     - the day and month names are written out by hand, because
 *       strftime's %a and %b are locale-dependent and a header a
 *       cache parses must not be at the mercy of the environment;
 *     - the Date names the moment the response was generated, not a
 *       moment baked into the code;
 *     - the errors carry one too -- the 404, the 400, and the
 *       chunked head.
 *
 * The oracle is Node: its captured response carries its own Date, in
 * the same format. Two implementations agreeing.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../src/http.h"
#include "../src/sockets.h"
#include "node_capture.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

static const http_host_route routes[] = {
    { "alpha.example", "alpha's document\n" }
};

#define ROUTE_COUNT (sizeof(routes) / sizeof(routes[0]))

static uint16_t spawn_server(pid_t *pid_out)
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
            http_serve_connection(fd, "<html>hi</html>\n", &st);
            sock_close(fd);
        }
        _exit(0);
    }
    *pid_out = pid;
    return l.port;
}

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
                                         "the default document\n", &st);
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

/* The value of the Date header, if the response carries one. */
static int date_value(const char *resp, char *out, size_t cap)
{
    const char *p = strstr(resp, "\r\nDate: ");
    const char *e;
    size_t len;

    if (p == NULL) {
        return -1;
    }
    p += 8; /* "\r\nDate: " */
    e = strstr(p, "\r\n");
    if (e == NULL) {
        return -1;
    }
    len = (size_t)(e - p);
    if (len + 1 > cap) {
        return -1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* Is this an IMF-fixdate? "Sun, 06 Nov 1994 08:49:37 GMT" */
static int is_imf_fixdate(const char *s)
{
    static const char *const days[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    char day[4];
    char mon[4];
    int i;

    if (s == NULL || strlen(s) != 29) {
        return 0;
    }
    if (s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' ||
        s[16] != ' ' || s[25] != ' ') {
        return 0;
    }
    memcpy(day, s, 3);
    day[3] = '\0';
    memcpy(mon, s + 8, 3);
    mon[3] = '\0';
    for (i = 0; i < 7; i++) {
        if (strcmp(day, days[i]) == 0) {
            break;
        }
    }
    if (i == 7) {
        return 0;
    }
    for (i = 0; i < 12; i++) {
        if (strcmp(mon, months[i]) == 0) {
            break;
        }
    }
    if (i == 12) {
        return 0;
    }
    if (!isdigit((unsigned char)s[5]) || !isdigit((unsigned char)s[6])) {
        return 0;
    }
    if (!isdigit((unsigned char)s[12]) || !isdigit((unsigned char)s[13]) ||
        !isdigit((unsigned char)s[14]) || !isdigit((unsigned char)s[15])) {
        return 0;
    }
    if (!isdigit((unsigned char)s[17]) || !isdigit((unsigned char)s[18]) ||
        s[19] != ':' ||
        !isdigit((unsigned char)s[20]) || !isdigit((unsigned char)s[21]) ||
        s[22] != ':' ||
        !isdigit((unsigned char)s[23]) || !isdigit((unsigned char)s[24])) {
        return 0;
    }
    if (memcmp(s + 26, "GMT", 3) != 0) {
        return 0;
    }
    return 1;
}

/* The epoch seconds an IMF-fixdate names, or -1 if it does not parse. */
static time_t fixdate_epoch(const char *s)
{
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    struct tm tm;
    char mon[4];
    int i;

    if (!is_imf_fixdate(s)) {
        return (time_t)-1;
    }
    memcpy(mon, s + 8, 3);
    mon[3] = '\0';
    for (i = 0; i < 12; i++) {
        if (strcmp(mon, months[i]) == 0) {
            break;
        }
    }
    if (i == 12) {
        return (time_t)-1;
    }
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = (s[12] - '0') * 1000 + (s[13] - '0') * 100 +
                 (s[14] - '0') * 10 + (s[15] - '0') - 1900;
    tm.tm_mon = i;
    tm.tm_mday = (s[5] - '0') * 10 + (s[6] - '0');
    tm.tm_hour = (s[17] - '0') * 10 + (s[18] - '0');
    tm.tm_min = (s[20] - '0') * 10 + (s[21] - '0');
    tm.tm_sec = (s[23] - '0') * 10 + (s[24] - '0');
    return timegm(&tm);
}

/* ------------------------------------------------------------------ */

static void test_a_response_carries_a_date(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];

    TEST("a response carries a Date header");
    port = spawn_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nHost: x.example\r\nConnection: close\r\n\r\n",
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
    if (date_value(resp, resp + 2000, 200) != 0) {
        FAIL("the response has no Date header");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_date_is_in_the_imf_fixdate_form(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];
    char date[128];

    TEST("the Date is in the IMF-fixdate form");
    port = spawn_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nHost: x.example\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (date_value(resp, date, sizeof(date)) != 0) {
        FAIL("the response has no Date header");
        waitpid(pid, NULL, 0);
        return;
    }
    if (!is_imf_fixdate(date)) {
        FAIL("the Date is not an IMF-fixdate");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_date_names_the_current_moment(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];
    char date[128];
    time_t when;
    time_t now;

    TEST("the Date names the current moment");
    port = spawn_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nHost: x.example\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (date_value(resp, date, sizeof(date)) != 0) {
        FAIL("the response has no Date header");
        waitpid(pid, NULL, 0);
        return;
    }
    when = fixdate_epoch(date);
    if (when == (time_t)-1) {
        FAIL("the Date does not parse");
        waitpid(pid, NULL, 0);
        return;
    }
    now = time(NULL);
    if (now - when > 2 || when - now > 2) {
        FAIL("the Date does not name the moment of the response");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_a_fixed_time_formats_exactly(void)
{
    char buf[64];
    int n;

    TEST("a fixed time formats exactly");
    n = http_date_format((time_t)784111777, buf, sizeof(buf));
    if (n < 0) {
        FAIL("the formatter refused a valid time");
        return;
    }
    /* The example RFC 9110 5.6.7 itself uses. */
    if (strcmp(buf, "Sun, 06 Nov 1994 08:49:37 GMT") != 0) {
        FAIL("the formatted date is not the RFC's own example");
        return;
    }
    PASS();
}

static void test_nodes_date_and_ours_have_the_same_shape(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];
    char ours[128];
    char node_head[NODE_HEAD_LEN + 1];
    char theirs[128];

    TEST("Node's Date and ours have the same shape");
    port = spawn_server(&pid);
    if (ask(port, "GET / HTTP/1.1\r\nHost: x.example\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (date_value(resp, ours, sizeof(ours)) != 0) {
        FAIL("the response has no Date header");
        waitpid(pid, NULL, 0);
        return;
    }
    memcpy(node_head, node_response, NODE_HEAD_LEN);
    node_head[NODE_HEAD_LEN] = '\0';
    if (date_value(node_head, theirs, sizeof(theirs)) != 0) {
        FAIL("Node's captured response has no Date header");
        waitpid(pid, NULL, 0);
        return;
    }
    if (!is_imf_fixdate(theirs)) {
        FAIL("Node's Date is not an IMF-fixdate");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strlen(theirs) != strlen(ours)) {
        FAIL("ours and Node's Date are different lengths");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_404_carries_a_date(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];
    char date[128];

    TEST("the 404 carries a Date too");
    port = spawn_server(&pid);
    if (ask(port, "GET /nope HTTP/1.1\r\nHost: x.example\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "HTTP/1.1 404 Not Found") == NULL) {
        FAIL("the status line is not 404");
        waitpid(pid, NULL, 0);
        return;
    }
    if (date_value(resp, date, sizeof(date)) != 0) {
        FAIL("the 404 has no Date header");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_400_carries_a_date(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];
    char date[128];

    TEST("the 400 carries a Date too");
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
    if (date_value(resp, date, sizeof(date)) != 0) {
        FAIL("the 400 has no Date header");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

static void test_the_chunked_response_carries_a_date(void)
{
    pid_t pid;
    uint16_t port;
    char resp[4096];
    char date[128];

    TEST("the chunked response carries a Date too");
    port = spawn_server(&pid);
    if (ask(port, "GET /stream HTTP/1.1\r\nHost: x.example\r\nConnection: close\r\n\r\n",
            resp, sizeof(resp)) != 0) {
        FAIL("the request failed");
        waitpid(pid, NULL, 0);
        return;
    }
    if (strstr(resp, "Transfer-Encoding: chunked") == NULL) {
        FAIL("the response is not chunked");
        waitpid(pid, NULL, 0);
        return;
    }
    if (date_value(resp, date, sizeof(date)) != 0) {
        FAIL("the chunked head has no Date header");
        waitpid(pid, NULL, 0);
        return;
    }
    waitpid(pid, NULL, 0);
    PASS();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Stage 65.17 -- the server sends Date:\n\n");

    test_a_response_carries_a_date();
    test_the_date_is_in_the_imf_fixdate_form();
    test_the_date_names_the_current_moment();
    test_a_fixed_time_formats_exactly();
    test_nodes_date_and_ours_have_the_same_shape();
    test_the_404_carries_a_date();
    test_the_400_carries_a_date();
    test_the_chunked_response_carries_a_date();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
