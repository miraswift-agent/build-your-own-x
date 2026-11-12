/*
 * Stage 65.3 tests: the accept loop with fork().
 *
 * The lesson: a server is a loop, and the loop is where the concurrency lives.
 *
 * Stage 65.1 made the descriptor visible. Stage 65.2 made the address visible.
 * This stage puts a process in the middle: one process accepts, one child per
 * connection, the child serves and exits, the parent reaps.
 *
 * The piece the earlier stages made visible is this: a forked child inherits
 * every descriptor its parent had open -- including the listening socket. If
 * the child keeps that copy, the socket never drains when the parent goes
 * away. If the parent closes its copy, the accept loop has nothing to accept
 * on. Both mistakes are silent, and both are caught here.
 *
 * Hermetic: loopback only, all processes local.
 */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/sockets.h"

static int tests_run = 0;
static int tests_passed = 0;

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
        printf("✗ %s\n", msg); \
    } while (0)

/* ------------------------------------------------------------------ */
/* The observation channel: the server and its children report back    */
/* through one pipe to the test process.                               */
/* ------------------------------------------------------------------ */

#define MSG_STARTED  1
#define MSG_FINISHED 2
#define MSG_FINAL    3

typedef struct {
    int   kind;
    int   index;
    pid_t handler_pid;
    pid_t handler_ppid;
    int   listener_valid;  /* inside a child: was the listener fd still open? */
    int   handler_rc;
    int   no_zombies;      /* in MSG_FINAL: no unreaped children left */
    sock_server server;    /* in MSG_FINAL */
} msg;

typedef struct {
    int msg_fd;       /* write end of the observation pipe */
    int go_fd;        /* read end of the release pipe */
    int fd_listener;  /* the listener's fd number, to check the child closed it */
    int block_index;  /* connection index whose handler waits for the release */
    int fail_index;   /* connection index whose handler fails */
} handler_ctx;

static void write_msg(int fd, const msg *m)
{
    ssize_t n = write(fd, m, sizeof(*m));
    (void)n; /* a small struct into a pipe is atomic */
}

static int read_msg(int fd, msg *m, int timeout_ms)
{
    struct pollfd pfd;
    ssize_t n;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        return -1;
    }
    n = read(fd, m, sizeof(*m));
    if (n != (ssize_t)sizeof(*m)) {
        return -1;
    }
    return 0;
}

/* Read observations until the server's final report arrives. */
static int wait_final(int fd, msg *out)
{
    for (;;) {
        if (read_msg(fd, out, 5000) != 0) {
            return -1;
        }
        if (out->kind == MSG_FINAL) {
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The handler that runs in each connection child.                     */
/* ------------------------------------------------------------------ */

static int test_handler(int conn_fd, int index, void *vctx)
{
    handler_ctx *ctx = (handler_ctx *)vctx;
    msg m;
    char buf[128];

    memset(&m, 0, sizeof m);
    m.kind = MSG_STARTED;
    m.index = index;
    m.handler_pid = getpid();
    m.handler_ppid = getppid();
    /* The child was forked with the listening socket open. It must have
     * closed its own copy before it started serving. */
    m.listener_valid = (fcntl(ctx->fd_listener, F_GETFD) != -1) ? 1 : 0;
    write_msg(ctx->msg_fd, &m);

    if (ctx->block_index == index) {
        struct pollfd pfd;
        char c;

        pfd.fd = ctx->go_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 5000) <= 0) {
            m.kind = MSG_FINISHED;
            m.handler_rc = -1;
            write_msg(ctx->msg_fd, &m);
            return -1;
        }
        if (read(ctx->go_fd, &c, 1) != 1) {
            m.kind = MSG_FINISHED;
            m.handler_rc = -1;
            write_msg(ctx->msg_fd, &m);
            return -1;
        }
    }

    if (ctx->fail_index == index) {
        m.kind = MSG_FINISHED;
        m.handler_rc = -1;
        write_msg(ctx->msg_fd, &m);
        return -1;
    }

    {
        ssize_t n = sock_read_once(conn_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            (void)sock_write_once(conn_fd, buf, (size_t)n);
        }
    }

    m.kind = MSG_FINISHED;
    m.handler_rc = 0;
    write_msg(ctx->msg_fd, &m);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Harness: fork a server, talk to it, collect what it observed.       */
/* ------------------------------------------------------------------ */

typedef struct {
    pid_t server_pid;
    uint16_t port;
    int obs_fd;      /* read end of the observation pipe */
    int go_w;        /* write end of the release pipe */
} harness;

/* Start the server in a child process. */
static int harness_start(harness *h, handler_ctx *ctx, int nconn,
                        int fail_index, int block_index)
{
    int obs[2];
    int go[2];
    sock_server s;
    pid_t p;

    if (pipe(obs) != 0) {
        return -1;
    }
    if (pipe(go) != 0) {
        close(obs[0]);
        close(obs[1]);
        return -1;
    }
    if (sock_server_start(&s, 0, 8) != 0) {
        close(obs[0]); close(obs[1]); close(go[0]); close(go[1]);
        return -1;
    }

    ctx->msg_fd = obs[1];
    ctx->go_fd = go[0];
    ctx->fd_listener = s.listener.listen_fd;
    ctx->block_index = block_index;
    ctx->fail_index = fail_index;

    p = fork();
    if (p < 0) {
        close(obs[0]); close(obs[1]); close(go[0]); close(go[1]);
        sock_close(s.listener.listen_fd);
        return -1;
    }
    if (p == 0) {
        /* the server process */
        msg m;
        int rc;
        int st;
        pid_t r;

        close(obs[0]);
        close(go[1]);
        rc = sock_server_serve(&s, test_handler, ctx, nconn);

        memset(&m, 0, sizeof m);
        m.kind = MSG_FINAL;
        m.server = s;
        m.listener_valid = (fcntl(s.listener.listen_fd, F_GETFD) != -1) ? 1 : 0;
        r = waitpid(-1, &st, WNOHANG);
        m.no_zombies = (r == -1 && errno == ECHILD) ? 1 : 0;
        write_msg(ctx->msg_fd, &m);

        sock_close(s.listener.listen_fd);
        _exit(rc == 0 ? 0 : 1);
    }

    /* the test process */
    close(obs[1]);
    close(go[0]);
    sock_close(s.listener.listen_fd);   /* only the server may hold the listener */

    h->server_pid = p;
    h->port = s.listener.port;
    h->obs_fd = obs[0];
    h->go_w = go[1];
    return 0;
}

static int harness_wait(harness *h, msg *final_msg)
{
    int status = 0;

    if (wait_final(h->obs_fd, final_msg) != 0) {
        return -1;
    }
    if (waitpid(h->server_pid, &status, 0) < 0) {
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

static void harness_stop(harness *h)
{
    close(h->obs_fd);
    close(h->go_w);
}

static int echo_round_trip(uint16_t port, const char *payload, char *out, size_t outlen)
{
    int fd = sock_connect_loopback(port);
    ssize_t n;

    if (fd < 0) {
        return -1;
    }
    n = sock_write_once(fd, payload, strlen(payload));
    if (n <= 0) {
        sock_close(fd);
        return -1;
    }
    n = sock_read_once(fd, out, outlen - 1);
    sock_close(fd);
    if (n <= 0) {
        return -1;
    }
    out[n] = '\0';
    return (int)n;
}

static int read_with_timeout(int fd, char *out, size_t outlen, int timeout_ms)
{
    struct pollfd pfd;
    ssize_t n;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        return -1;
    }
    n = sock_read_once(fd, out, outlen - 1);
    if (n <= 0) {
        return -1;
    }
    out[n] = '\0';
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* 1. The child serves the connection.                                  */
/* ------------------------------------------------------------------ */
static void test_a_forked_child_serves_the_connection(void)
{
    harness h;
    handler_ctx ctx;
    msg final_msg;
    char buf[64];

    TEST("a_forked_child_serves_the_connection");
    if (harness_start(&h, &ctx, 1, -1, -1) != 0) {
        FAIL("could not start the server");
        return;
    }
    if (echo_round_trip(h.port, "ping", buf, sizeof(buf)) < 0) {
        FAIL("the connection was not served");
        harness_stop(&h);
        return;
    }
    if (strcmp(buf, "ping") != 0) {
        FAIL("the bytes did not survive the round trip");
        harness_stop(&h);
        return;
    }
    if (harness_wait(&h, &final_msg) != 0) {
        FAIL("the server did not finish cleanly");
        harness_stop(&h);
        return;
    }
    harness_stop(&h);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 2. The child is a different process, and the server is its parent.   */
/* ------------------------------------------------------------------ */
static void test_the_child_runs_in_its_own_process(void)
{
    harness h;
    handler_ctx ctx;
    msg final_msg;
    msg m;
    int saw_started = 0;

    TEST("the_child_runs_in_its_own_process");
    if (harness_start(&h, &ctx, 1, -1, -1) != 0) {
        FAIL("could not start the server");
        return;
    }
    /* A child only exists once a connection has been accepted, so open one.
     * It is closed straight away: the child then reads end-of-file, finishes
     * and exits, which lets the server finish too. */
    {
        int c = sock_connect_loopback(h.port);
        if (c < 0) {
            FAIL("could not connect");
            harness_stop(&h);
            return;
        }
        sock_close(c);
    }
    /* wait for the child's own report */
    while (read_msg(h.obs_fd, &m, 5000) == 0) {
        if (m.kind == MSG_STARTED) {
            saw_started = 1;
            break;
        }
    }
    if (!saw_started) {
        FAIL("the child never reported in");
        harness_stop(&h);
        return;
    }
    if (m.handler_pid == h.server_pid) {
        FAIL("the handler ran in the server process, not a child");
        harness_stop(&h);
        return;
    }
    if (m.handler_ppid != h.server_pid) {
        FAIL("the handler's parent is not the server process");
        harness_stop(&h);
        return;
    }
    if (harness_wait(&h, &final_msg) != 0) {
        FAIL("the server did not finish cleanly");
        harness_stop(&h);
        return;
    }
    if (final_msg.server.last_child != m.handler_pid) {
        FAIL("the server did not record the child's pid");
        harness_stop(&h);
        return;
    }
    harness_stop(&h);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 3. The child closes its copy of the listening socket.                */
/* ------------------------------------------------------------------ */
static void test_the_child_closes_its_copy_of_the_listener(void)
{
    harness h;
    handler_ctx ctx;
    msg final_msg;
    msg m;
    int saw_started = 0;

    TEST("the_child_closes_its_copy_of_the_listener");
    if (harness_start(&h, &ctx, 1, -1, -1) != 0) {
        FAIL("could not start the server");
        return;
    }
    /* A child only exists once a connection has been accepted, so open one.
     * It is closed straight away: the child then reads end-of-file, finishes
     * and exits, which lets the server finish too. */
    {
        int c = sock_connect_loopback(h.port);
        if (c < 0) {
            FAIL("could not connect");
            harness_stop(&h);
            return;
        }
        sock_close(c);
    }
    while (read_msg(h.obs_fd, &m, 5000) == 0) {
        if (m.kind == MSG_STARTED) {
            saw_started = 1;
            break;
        }
    }
    if (!saw_started) {
        FAIL("the child never reported in");
        harness_stop(&h);
        return;
    }
    if (m.listener_valid != 0) {
        FAIL("the child still held the listening socket open");
        harness_stop(&h);
        return;
    }
    if (harness_wait(&h, &final_msg) != 0) {
        FAIL("the server did not finish cleanly");
        harness_stop(&h);
        return;
    }
    harness_stop(&h);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 4. The parent keeps its copy of the listening socket.                */
/* ------------------------------------------------------------------ */
static void test_the_parent_keeps_its_listener_open(void)
{
    harness h;
    handler_ctx ctx;
    msg final_msg;

    TEST("the_parent_keeps_its_listener_open");
    if (harness_start(&h, &ctx, 1, -1, -1) != 0) {
        FAIL("could not start the server");
        return;
    }
    /* A child only exists once a connection has been accepted, so open one.
     * It is closed straight away: the child then reads end-of-file, finishes
     * and exits, which lets the server finish too. */
    {
        int c = sock_connect_loopback(h.port);
        if (c < 0) {
            FAIL("could not connect");
            harness_stop(&h);
            return;
        }
        sock_close(c);
    }
    if (harness_wait(&h, &final_msg) != 0) {
        FAIL("the server did not finish cleanly");
        harness_stop(&h);
        return;
    }
    if (final_msg.listener_valid != 1) {
        FAIL("the server closed its own listening socket");
        harness_stop(&h);
        return;
    }
    harness_stop(&h);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 5. A server is a loop: three connections, three correct answers.    */
/* ------------------------------------------------------------------ */
static void test_the_server_serves_connections_in_a_loop(void)
{
    harness h;
    handler_ctx ctx;
    msg final_msg;
    char buf[64];
    const char *payloads[3];
    int i;

    TEST("the_server_serves_connections_in_a_loop");
    payloads[0] = "one";
    payloads[1] = "two";
    payloads[2] = "three";

    if (harness_start(&h, &ctx, 3, -1, -1) != 0) {
        FAIL("could not start the server");
        return;
    }
    for (i = 0; i < 3; i++) {
        if (echo_round_trip(h.port, payloads[i], buf, sizeof(buf)) < 0) {
            FAIL("a connection was not served");
            harness_stop(&h);
            return;
        }
        if (strcmp(buf, payloads[i]) != 0) {
            FAIL("the wrong bytes came back");
            harness_stop(&h);
            return;
        }
    }
    if (harness_wait(&h, &final_msg) != 0) {
        FAIL("the server did not finish cleanly");
        harness_stop(&h);
        return;
    }
    if (final_msg.server.connections != 3) {
        FAIL("the server did not count three connections");
        harness_stop(&h);
        return;
    }
    harness_stop(&h);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 6. The parent reaps every child: no zombies left behind.            */
/* ------------------------------------------------------------------ */
static void test_the_parent_reaps_every_child(void)
{
    harness h;
    handler_ctx ctx;
    msg final_msg;
    char buf[64];

    TEST("the_parent_reaps_every_child");
    if (harness_start(&h, &ctx, 2, -1, -1) != 0) {
        FAIL("could not start the server");
        return;
    }
    if (echo_round_trip(h.port, "a", buf, sizeof(buf)) < 0 ||
        echo_round_trip(h.port, "b", buf, sizeof(buf)) < 0) {
        FAIL("the connections were not served");
        harness_stop(&h);
        return;
    }
    if (harness_wait(&h, &final_msg) != 0) {
        FAIL("the server did not finish cleanly");
        harness_stop(&h);
        return;
    }
    if (final_msg.no_zombies != 1) {
        FAIL("the server left unreaped children behind");
        harness_stop(&h);
        return;
    }
    if (final_msg.server.children_reaped != 2) {
        FAIL("the server did not reap both children");
        harness_stop(&h);
        return;
    }
    harness_stop(&h);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 7. The parent accepts the next connection while a child is serving.  */
/*    This is the concurrency, and it is the whole point of the stage.   */
/* ------------------------------------------------------------------ */
static void test_the_parent_accepts_while_a_child_is_serving(void)
{
    harness h;
    handler_ctx ctx;
    msg final_msg;
    msg m;
    char buf1[64];
    char buf2[64];
    int conn1 = -1;
    int conn2 = -1;
    int got2 = -1;
    int got1 = -1;
    int saw_started = 0;

    TEST("the_parent_accepts_while_a_child_is_serving");
    if (harness_start(&h, &ctx, 2, -1, 0) != 0) {
        FAIL("could not start the server");
        return;
    }

    conn1 = sock_connect_loopback(h.port);
    if (conn1 < 0) {
        FAIL("could not connect");
        harness_stop(&h);
        return;
    }
    (void)sock_write_once(conn1, "one", 3);

    /* wait until the first child is inside the handler and blocked */
    while (read_msg(h.obs_fd, &m, 5000) == 0) {
        if (m.kind == MSG_STARTED && m.index == 0) {
            saw_started = 1;
            break;
        }
    }
    if (!saw_started) {
        FAIL("the first child never reached the handler");
        goto cleanup;
    }

    /* the second connection: if the server is concurrent, it is served while
     * the first child is still blocked */
    conn2 = sock_connect_loopback(h.port);
    if (conn2 < 0) {
        FAIL("could not connect a second time");
        goto cleanup;
    }
    (void)sock_write_once(conn2, "two", 3);
    got2 = read_with_timeout(conn2, buf2, sizeof(buf2), 1500);

    /* release the blocked handler, whatever happened above */
    (void)write(h.go_w, "g", 1);
    got1 = read_with_timeout(conn1, buf1, sizeof(buf1), 3000);

    if (got2 <= 0) {
        FAIL("the second connection was not served while the first was blocked");
        goto cleanup;
    }
    if (strcmp(buf2, "two") != 0) {
        FAIL("the second connection returned the wrong bytes");
        goto cleanup;
    }
    if (got1 <= 0 || strcmp(buf1, "one") != 0) {
        FAIL("the first connection was not served correctly");
        goto cleanup;
    }
    if (harness_wait(&h, &final_msg) != 0) {
        FAIL("the server did not finish cleanly");
        goto cleanup;
    }
    harness_stop(&h);
    PASS();
    return;

cleanup:
    if (conn1 >= 0) sock_close(conn1);
    if (conn2 >= 0) sock_close(conn2);
    harness_stop(&h);
}

/* ------------------------------------------------------------------ */
/* 8. One failing handler does not stop the loop.                       */
/* ------------------------------------------------------------------ */
static void test_a_failing_handler_does_not_stop_the_loop(void)
{
    harness h;
    handler_ctx ctx;
    msg final_msg;
    char buf[64];
    int conn = -1;

    TEST("a_failing_handler_does_not_stop_the_loop");
    if (harness_start(&h, &ctx, 3, 1, -1) != 0) {
        FAIL("could not start the server");
        return;
    }

    if (echo_round_trip(h.port, "one", buf, sizeof(buf)) < 0 ||
        strcmp(buf, "one") != 0) {
        FAIL("the first connection was not served");
        harness_stop(&h);
        return;
    }

    conn = sock_connect_loopback(h.port);
    if (conn < 0) {
        FAIL("could not connect");
        harness_stop(&h);
        return;
    }
    (void)sock_write_once(conn, "two", 3);
    /* the failing handler serves nothing: the client sees end of stream */
    if (read_with_timeout(conn, buf, sizeof(buf), 2000) > 0) {
        FAIL("the failing handler still answered");
        sock_close(conn);
        harness_stop(&h);
        return;
    }
    sock_close(conn);

    if (echo_round_trip(h.port, "three", buf, sizeof(buf)) < 0 ||
        strcmp(buf, "three") != 0) {
        FAIL("the loop stopped after a failing handler");
        harness_stop(&h);
        return;
    }
    if (harness_wait(&h, &final_msg) != 0) {
        FAIL("the server did not finish cleanly");
        harness_stop(&h);
        return;
    }
    if (final_msg.server.children_failed != 1) {
        FAIL("the server did not notice the failing child");
        harness_stop(&h);
        return;
    }
    if (final_msg.server.connections != 3) {
        FAIL("the server did not serve all three connections");
        harness_stop(&h);
        return;
    }
    harness_stop(&h);
    PASS();
}

int main(void)
{
    printf("Stage 65.3 — the accept loop with fork()\n\n");

    /* A write to a pipe whose reader has gone must not kill the test. */
    signal(SIGPIPE, SIG_IGN);

    test_a_forked_child_serves_the_connection();
    test_the_child_runs_in_its_own_process();
    test_the_child_closes_its_copy_of_the_listener();
    test_the_parent_keeps_its_listener_open();
    test_the_server_serves_connections_in_a_loop();
    test_the_parent_reaps_every_child();
    test_the_parent_accepts_while_a_child_is_serving();
    test_a_failing_handler_does_not_stop_the_loop();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
