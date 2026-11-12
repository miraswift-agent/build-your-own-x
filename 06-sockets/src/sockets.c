/*
 * Stage 65.1 — the fd lifecycle.
 * Stage 65.2 — addresses by hand.
 *
 * Nothing here is clever. That is the point: the descriptor is the whole
 * interface, and every mistake that matters later in this arc comes from
 * forgetting that the kernel, not the process, owns the connection.
 *
 * Stage 65.2 adds the address layer. The endpoint is now a byte string we
 * assemble ourselves -- two bytes for the port in a stated order, four bytes
 * for the address -- and the kernel's struct is only ever populated from
 * those bytes, or read back out of them. No library helper is trusted to do
 * the conversion; that is the whole point of the stage.
 */

#include "sockets.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Addresses, by hand.                                                 */
/* ------------------------------------------------------------------ */

/* Two bytes, high byte first: the order the network uses. */
static void put_port_bytes(uint8_t out[2], uint16_t port)
{
    out[0] = (uint8_t)((port >> 8) & 0xFF);
    out[1] = (uint8_t)(port & 0xFF);
}

int sock_ipv4_loopback_bytes(uint16_t port, sock_ipv4_bytes *out)
{
    if (out == NULL) {
        errno = EINVAL;
        return -1;
    }
    put_port_bytes(out->port, port);
    out->ip[0] = 127;
    out->ip[1] = 0;
    out->ip[2] = 0;
    out->ip[3] = 1;
    return 0;
}

int sock_addr_apply(const sock_ipv4_bytes *b, struct sockaddr_in *out)
{
    uint8_t *raw_port;

    if (b == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(out, 0, sizeof(*out));

    /* The one exception: the family field is a host-order number, not a
     * network byte string. POSIX says so, and the kernel reads it that way. */
    out->sin_family = AF_INET;

    /* The port and address fields hold their bytes in network order already,
     * so the bytes we built go straight in -- no conversion step, no library
     * helper, no assumption about this machine's byte order. */
    raw_port = (uint8_t *)&out->sin_port;
    raw_port[0] = b->port[0];
    raw_port[1] = b->port[1];
    memcpy(&out->sin_addr, b->ip, 4);
    return 0;
}

int sock_addr_extract(const struct sockaddr_in *in, sock_ipv4_bytes *out)
{
    const uint8_t *raw_port;

    if (in == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    raw_port = (const uint8_t *)&in->sin_port;
    out->port[0] = raw_port[0];
    out->port[1] = raw_port[1];
    memcpy(out->ip, &in->sin_addr, 4);
    return 0;
}

uint16_t sock_port_from_bytes(const uint8_t port[2])
{
    return (uint16_t)(((uint16_t)port[0] << 8) | (uint16_t)port[1]);
}

/* ------------------------------------------------------------------ */
/* The fd lifecycle.                                                   */
/* ------------------------------------------------------------------ */

int sock_listen_loopback(sock_listener *out, uint16_t port, int backlog)
{
    sock_ipv4_bytes bytes;
    sock_ipv4_bytes bound_bytes;
    struct sockaddr_in addr;
    struct sockaddr_in bound;
    socklen_t bound_len = sizeof(bound);
    int fd;
    int one = 1;

    if (out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sock_ipv4_loopback_bytes(port, &bytes) != 0) {
        return -1;
    }
    if (sock_addr_apply(&bytes, &addr) != 0) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    /* Without SO_REUSEADDR a port still in TIME_WAIT refuses a new bind. */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    if (listen(fd, backlog) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    /* Ask the kernel which port it actually gave us. With port 0 we did not
     * choose one, and a chosen-but-unverified port is a bug waiting. */
    if (getsockname(fd, (struct sockaddr *)&bound, &bound_len) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    if (sock_addr_extract(&bound, &bound_bytes) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    out->listen_fd = fd;
    out->port = sock_port_from_bytes(bound_bytes.port);
    return 0;
}

int sock_accept(const sock_listener *l)
{
    if (l == NULL) {
        errno = EINVAL;
        return -1;
    }
    return accept(l->listen_fd, NULL, NULL);
}

int sock_connect_loopback(uint16_t port)
{
    sock_ipv4_bytes bytes;
    struct sockaddr_in addr;
    int fd;

    if (sock_ipv4_loopback_bytes(port, &bytes) != 0) {
        return -1;
    }
    if (sock_addr_apply(&bytes, &addr) != 0) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    return fd;
}

ssize_t sock_read_once(int fd, void *buf, size_t len)
{
    return read(fd, buf, len);
}

ssize_t sock_write_once(int fd, const void *buf, size_t len)
{
    return write(fd, buf, len);
}

int sock_close(int fd)
{
    return close(fd);
}

/* ------------------------------------------------------------------ */
/* Stage 65.3 — the accept loop.                                       */
/* ------------------------------------------------------------------ */

int sock_server_start(sock_server *s, uint16_t port, int backlog)
{
    if (s == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(s, 0, sizeof(*s));
    return sock_listen_loopback(&s->listener, port, backlog);
}

/* Reap whatever children have finished. The non-blocking form is used inside
 * the accept loop, because a child that is still serving must never hold up
 * the next accept. */
static void sock_reap_children(sock_server *s, int blocking)
{
    int status;
    pid_t done;

    while ((done = waitpid(-1, &status, blocking ? 0 : WNOHANG)) > 0) {
        s->children_reaped++;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            s->children_failed++;
        }
    }
}

int sock_server_serve(sock_server *s, sock_conn_handler handler, void *ctx,
                      int max_connections)
{
    int i;

    if (s == NULL || handler == NULL || max_connections < 0) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < max_connections; i++) {
        int conn;
        pid_t pid;

        do {
            conn = sock_accept(&s->listener);
        } while (conn < 0 && errno == EINTR);
        if (conn < 0) {
            return -1;
        }

        pid = fork();
        if (pid < 0) {
            int saved = errno;
            sock_close(conn);
            errno = saved;
            return -1;
        }

        if (pid == 0) {
            /* The child. It inherited the listening socket, and it does not
             * need it: a child that keeps the listener open keeps the port
             * alive after the parent is gone, and the socket never drains.
             * The parent needs it -- it is the only accept path. */
            int rc;

            sock_close(s->listener.listen_fd);
            rc = handler(conn, i, ctx);
            sock_close(conn);
            _exit(rc == 0 ? 0 : 1);
        }

        /* The parent. It does not need the connection -- the child owns it
         * now -- and it must not close the listener. */
        sock_close(conn);
        s->last_child = pid;
        s->connections++;

        /* Reap without blocking: a child still serving must never hold up
         * the next accept. This is what makes the server concurrent. */
        sock_reap_children(s, 0);
    }

    /* Every connection has been served; wait for the children still running
     * so that no zombie is left behind. */
    sock_reap_children(s, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Stage 65.4 — errors as protocol.                                    */
/* ------------------------------------------------------------------ */

const char *sock_io_status_name(sock_io_status st)
{
    switch (st) {
    case SOCK_IO_COMPLETE:    return "complete";
    case SOCK_IO_PARTIAL:     return "partial";
    case SOCK_IO_EOF:         return "eof";
    case SOCK_IO_INTERRUPTED: return "interrupted";
    case SOCK_IO_WOULD_BLOCK: return "would-block";
    case SOCK_IO_ERROR:       return "error";
    }
    return "unknown";
}

/* Fill a result. Every path in read_some / write_some goes through here, so
 * a caller can trust that a result is never left half-written. */
static void sock_io_set(sock_io_result *out, sock_io_status status,
                        size_t transferred, int err)
{
    out->status = status;
    out->transferred = transferred;
    out->err = err;
}

/* Turn one syscall outcome into a status. This is the whole stage: the
 * outcome becomes data, and errno stops being a global the caller has to
 * remember to read before it does anything else. */
static sock_io_status sock_io_status_of(int failed, int saved_errno,
                                        size_t got, size_t asked)
{
    if (!failed) {
        if (got == 0) {
            return SOCK_IO_EOF;
        }
        return (got == asked) ? SOCK_IO_COMPLETE : SOCK_IO_PARTIAL;
    }
    if (saved_errno == EINTR) {
        return SOCK_IO_INTERRUPTED;
    }
    if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
        return SOCK_IO_WOULD_BLOCK;
    }
    return SOCK_IO_ERROR;
}

int sock_read_some(int fd, void *buf, size_t len, sock_io_result *out)
{
    ssize_t n;
    int saved;

    if (out == NULL || (buf == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0) {
        /* A zero-length request is trivially satisfied. read() would answer
         * 0 here, which we would otherwise have to read as EOF. */
        sock_io_set(out, SOCK_IO_COMPLETE, 0, 0);
        return 0;
    }

    n = read(fd, buf, len);
    saved = errno;  /* captured now: the next call may clobber it */

    {
        sock_io_status st = sock_io_status_of(n < 0, saved,
                                              (n > 0) ? (size_t)n : 0, len);
        /* err carries errno ONLY for a real error. EINTR and EAGAIN are
         * not failures: filling err for them would make `err != 0` mean
         * "something happened, sometimes harmless" -- the exact confusion
         * this stage exists to remove. The status already names them. */
        sock_io_set(out, st, (n > 0) ? (size_t)n : 0,
                    (st == SOCK_IO_ERROR) ? saved : 0);
    }
    return 0;
}

int sock_write_some(int fd, const void *buf, size_t len, sock_io_result *out)
{
    ssize_t n;
    int saved;

    if (out == NULL || (buf == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0) {
        sock_io_set(out, SOCK_IO_COMPLETE, 0, 0);
        return 0;
    }

    n = write(fd, buf, len);
    saved = errno;  /* captured now: the next call may clobber it */

    {
        sock_io_status st = sock_io_status_of(n < 0, saved,
                                              (n > 0) ? (size_t)n : 0, len);
        /* err carries errno ONLY for a real error. EINTR and EAGAIN are
         * not failures: filling err for them would make `err != 0` mean
         * "something happened, sometimes harmless" -- the exact confusion
         * this stage exists to remove. The status already names them. */
        sock_io_set(out, st, (n > 0) ? (size_t)n : 0,
                    (st == SOCK_IO_ERROR) ? saved : 0);
    }
    return 0;
}

int sock_read_exact(int fd, void *buf, size_t len, sock_io_result *out)
{
    size_t done = 0;

    if (out == NULL || (buf == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0) {
        sock_io_set(out, SOCK_IO_COMPLETE, 0, 0);
        return 0;
    }

    while (done < len) {
        sock_io_result r;

        if (sock_read_some(fd, (char *)buf + done, len - done, &r) != 0) {
            return -1;
        }
        if (r.status == SOCK_IO_ERROR || r.status == SOCK_IO_WOULD_BLOCK ||
            r.status == SOCK_IO_EOF) {
            /* Nothing to fix by trying again. Report it, and report how far
             * we got -- a partial read that stopped early is still data the
             * caller has. */
            sock_io_set(out, r.status, done + r.transferred, r.err);
            return 0;
        }
        if (r.status == SOCK_IO_INTERRUPTED) {
            continue;  /* a signal, not a failure: go again */
        }
        done += r.transferred;
    }

    sock_io_set(out, SOCK_IO_COMPLETE, done, 0);
    return 0;
}

int sock_write_exact(int fd, const void *buf, size_t len, sock_io_result *out)
{
    size_t done = 0;

    if (out == NULL || (buf == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0) {
        sock_io_set(out, SOCK_IO_COMPLETE, 0, 0);
        return 0;
    }

    while (done < len) {
        sock_io_result r;

        if (sock_write_some(fd, (const char *)buf + done, len - done, &r) != 0) {
            return -1;
        }
        if (r.status == SOCK_IO_ERROR || r.status == SOCK_IO_WOULD_BLOCK) {
            /* A short write is a silent truncation unless the result says so.
             * This is where it says so: "would block" after N bytes means the
             * caller has N bytes written and must come back for the rest. */
            sock_io_set(out, r.status, done + r.transferred, r.err);
            return 0;
        }
        if (r.status == SOCK_IO_INTERRUPTED) {
            continue;  /* a signal, not a failure: go again */
        }
        done += r.transferred;
    }

    sock_io_set(out, SOCK_IO_COMPLETE, done, 0);
    return 0;
}
