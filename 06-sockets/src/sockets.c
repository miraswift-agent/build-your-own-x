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

/* _DEFAULT_SOURCE exposes O_NONBLOCK, select() and MSG_NOSIGNAL under
 * -std=c11, which otherwise hides them behind __STRICT_ANSI__. */
#define _DEFAULT_SOURCE

#include "sockets.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
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

/* ------------------------------------------------------------------ */
/* Stage 65.5 -- non-blocking, multiplexed, framed.                    */
/* ------------------------------------------------------------------ */

int sock_set_nonblocking(int fd, int nonblocking)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags);
}

void sock_frame_init(sock_frame *f)
{
    if (f == NULL) {
        return;
    }
    f->len = 0;
    f->off = 0;
}

int sock_frame_feed(sock_frame *f, const void *data, size_t len)
{
    if (f == NULL || (data == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (f->off > 0) {
        /* Drop the consumed prefix. This moves the bytes, so a pointer
         * handed out by the previous sock_frame_next() is stale after
         * this call -- the contract says so. */
        memmove(f->buf, f->buf + f->off, f->len - f->off);
        f->len -= f->off;
        f->off = 0;
    }
    if (f->len + len > SOCK_FRAME_BUF) {
        errno = ENOBUFS;
        return -1;
    }
    if (len > 0) {
        memcpy(f->buf + f->len, data, len);
    }
    f->len += len;
    return 0;
}

int sock_frame_next(sock_frame *f, const char **out, uint32_t *len)
{
    size_t avail;
    const uint8_t *h;
    uint32_t payload_len;

    if (f == NULL) {
        errno = EINVAL;
        return -1;
    }
    avail = f->len - f->off;
    if (avail < 4) {
        return 0;  /* not even a header yet */
    }
    h = (const uint8_t *)(f->buf + f->off);
    payload_len = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) |
                  ((uint32_t)h[2] << 8) | (uint32_t)h[3];

    if (payload_len > SOCK_FRAME_MAX) {
        /* The header alone is enough to know the peer is speaking a
         * protocol we do not accept. Do not wait for a payload we would
         * refuse anyway. */
        errno = EMSGSIZE;
        return -1;
    }
    if (avail < 4 + (size_t)payload_len) {
        return 0;  /* the header is here; the body is not */
    }
    if (out != NULL) {
        *out = f->buf + f->off + 4;
    }
    if (len != NULL) {
        *len = payload_len;
    }
    f->off += 4 + (size_t)payload_len;
    return 1;
}

int sock_frame_encode(char *dst, size_t dst_len, const void *payload, uint32_t len)
{
    if (dst == NULL || (payload == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (dst_len < 4 + (size_t)len) {
        errno = ENOBUFS;
        return -1;
    }
    dst[0] = (char)((len >> 24) & 0xFF);
    dst[1] = (char)((len >> 16) & 0xFF);
    dst[2] = (char)((len >> 8) & 0xFF);
    dst[3] = (char)(len & 0xFF);
    if (len > 0) {
        memcpy(dst + 4, payload, len);
    }
    return (int)(4 + (size_t)len);
}

static int mux_free_slot(const sock_mux *m)
{
    int i;

    for (i = 0; i < SOCK_MAX_CONNS; i++) {
        if (!m->conns[i].in_use) {
            return i;
        }
    }
    return -1;
}

static void mux_close_conn(sock_mux *m, int slot)
{
    sock_conn *c = &m->conns[slot];

    if (!c->in_use) {
        return;
    }
    sock_close(c->fd);
    c->fd = -1;
    c->in_use = 0;
    c->closing = 0;
    c->out_len = 0;
    c->out_off = 0;
    sock_frame_init(&c->frame);
    m->nconns--;
}

int sock_mux_start(sock_mux *m, uint16_t port, int backlog)
{
    int i;

    if (m == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(m, 0, sizeof(*m));
    for (i = 0; i < SOCK_MAX_CONNS; i++) {
        m->conns[i].fd = -1;
    }
    if (sock_listen_loopback(&m->listener, port, backlog) != 0) {
        return -1;
    }
    if (sock_set_nonblocking(m->listener.listen_fd, 1) != 0) {
        sock_close(m->listener.listen_fd);
        m->listener.listen_fd = -1;
        return -1;
    }
    return 0;
}

int sock_mux_accept(sock_mux *m)
{
    int accepted = 0;

    for (;;) {
        int fd = sock_accept(&m->listener);
        int slot;

        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  /* the queue is empty -- the normal case */
            }
            return -1;
        }
        slot = mux_free_slot(m);
        if (slot < 0) {
            sock_close(fd);
            break;  /* at capacity: refuse this one too */
        }
        if (sock_set_nonblocking(fd, 1) != 0) {
            sock_close(fd);
            return -1;
        }
        memset(&m->conns[slot], 0, sizeof(m->conns[slot]));
        m->conns[slot].fd = fd;
        m->conns[slot].in_use = 1;
        sock_frame_init(&m->conns[slot].frame);
        m->nconns++;
        m->accepted++;
        accepted++;
    }
    return accepted;
}

int sock_conn_queue(sock_conn *c, const void *data, size_t len)
{
    if (c == NULL || (data == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (c->out_off > 0) {
        memmove(c->out_buf, c->out_buf + c->out_off, c->out_len - c->out_off);
        c->out_len -= c->out_off;
        c->out_off = 0;
    }
    if (c->out_len + len > SOCK_CONN_BUF) {
        errno = ENOBUFS;
        return -1;
    }
    if (len > 0) {
        memcpy(c->out_buf + c->out_len, data, len);
    }
    c->out_len += len;
    return 0;
}

int sock_conn_queue_frame(sock_conn *c, const void *payload, uint32_t len)
{
    uint8_t hdr[4];

    if (c == NULL || (payload == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    hdr[0] = (uint8_t)((len >> 24) & 0xFF);
    hdr[1] = (uint8_t)((len >> 16) & 0xFF);
    hdr[2] = (uint8_t)((len >> 8) & 0xFF);
    hdr[3] = (uint8_t)(len & 0xFF);

    if (c->out_off > 0) {
        memmove(c->out_buf, c->out_buf + c->out_off, c->out_len - c->out_off);
        c->out_len -= c->out_off;
        c->out_off = 0;
    }
    if (c->out_len + 4 + (size_t)len > SOCK_CONN_BUF) {
        errno = ENOBUFS;
        return -1;
    }
    memcpy(c->out_buf + c->out_len, hdr, 4);
    if (len > 0) {
        memcpy(c->out_buf + c->out_len + 4, payload, len);
    }
    c->out_len += 4 + (size_t)len;
    return 0;
}

/* Write out whatever is queued. A non-blocking write that cannot finish
 * leaves the rest queued for the next poll -- the connection is not
 * dropped for being slow, which is the whole point of the stage. */
static int conn_flush(sock_conn *c)
{
    while (c->out_off < c->out_len) {
        ssize_t n = send(c->fd, c->out_buf + c->out_off,
                         c->out_len - c->out_off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;  /* the socket is full; come back later */
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        c->out_off += (size_t)n;
        c->bytes_out += (unsigned long)n;
    }
    c->out_len = 0;
    c->out_off = 0;
    return 0;
}

/* Read what is available and hand every complete message to the handler.
 * Returns 0 on success, -1 on a protocol or socket error. */
static int conn_service(sock_conn *c, sock_msg_handler handler, void *ctx)
{
    char buf[1024];
    ssize_t n = read(c->fd, buf, sizeof buf);

    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* nothing to do now; not an error */
        }
        return -1;
    }
    if (n == 0) {
        /* The peer closed its half. We may still owe it a reply, so this is
         * not a close yet -- it is "drain and go". */
        c->closing = 1;
        return 0;
    }
    if (sock_frame_feed(&c->frame, buf, (size_t)n) != 0) {
        return -1;
    }
    for (;;) {
        const char *msg = NULL;
        uint32_t mlen = 0;
        int r = sock_frame_next(&c->frame, &msg, &mlen);

        if (r < 0) {
            return -1;  /* a length we do not accept: the peer is wrong */
        }
        if (r == 0) {
            break;  /* need more bytes */
        }
        c->messages_in++;
        if (handler != NULL) {
            handler(c, msg, mlen, ctx);
        }
    }
    return 0;
}

int sock_mux_poll(sock_mux *m, sock_msg_handler handler, void *ctx, int timeout_ms)
{
    fd_set rfds;
    fd_set wfds;
    struct timeval tv;
    int maxfd = -1;
    int events = 0;
    int i;

    if (m == NULL) {
        errno = EINVAL;
        return -1;
    }

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_SET(m->listener.listen_fd, &rfds);
    maxfd = m->listener.listen_fd;
    for (i = 0; i < SOCK_MAX_CONNS; i++) {
        sock_conn *c = &m->conns[i];
        if (!c->in_use) {
            continue;
        }
        FD_SET(c->fd, &rfds);
        if (c->fd > maxfd) {
            maxfd = c->fd;
        }
        if (c->out_len > c->out_off) {
            FD_SET(c->fd, &wfds);
        }
    }

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(maxfd + 1, &rfds, &wfds, NULL, &tv) < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }

    if (FD_ISSET(m->listener.listen_fd, &rfds)) {
        int n = sock_mux_accept(m);
        if (n < 0) {
            return -1;
        }
        events += n;
    }

    for (i = 0; i < SOCK_MAX_CONNS; i++) {
        sock_conn *c = &m->conns[i];
        if (!c->in_use || !FD_ISSET(c->fd, &rfds)) {
            continue;
        }
        if (conn_service(c, handler, ctx) != 0) {
            mux_close_conn(m, i);
        }
        events++;
    }

    /* A separate write pass: the handler may have queued a reply while we
     * were reading, and that connection was not in the write set when
     * select() ran. */
    for (i = 0; i < SOCK_MAX_CONNS; i++) {
        sock_conn *c = &m->conns[i];
        if (!c->in_use) {
            continue;
        }
        if (c->out_len > c->out_off) {
            if (conn_flush(c) != 0) {
                mux_close_conn(m, i);
                events++;
                continue;
            }
        }
        if (c->closing && c->out_off >= c->out_len) {
            mux_close_conn(m, i);
            events++;
        }
    }
    return events;
}
