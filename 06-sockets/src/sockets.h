/*
 * Stage 65.1 — the fd lifecycle.
 * Stage 65.2 — addresses by hand.
 *
 * A socket is a file descriptor: an integer the kernel hands us that stands
 * for a kernel object. Everything in this component is a consequence of that
 * one fact, so Stage 65.1 is deliberately thin -- it is the vocabulary the
 * later stages use.
 *
 * Stage 65.2 removes the last convenience: the address is no longer a name
 * ("127.0.0.1") and a number (a port) that the library converts for us. It is
 * now a byte string we build ourselves, so that we can point at it, compare
 * it, and hand it to the kernel unchanged.
 */

#ifndef SOCKETS_H
#define SOCKETS_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* A listening TCP socket bound to loopback. */
typedef struct {
    int      listen_fd;  /* the listening descriptor */
    uint16_t port;       /* the port actually bound (read back from the kernel) */
} sock_listener;

/* An IPv4 endpoint as the bytes the kernel expects.
 *
 * port[0] is the high byte and port[1] the low byte -- network byte order,
 * spelled out rather than delegated. ip is one byte per octet. */
typedef struct {
    uint8_t port[2];
    uint8_t ip[4];
} sock_ipv4_bytes;

/* Build the loopback endpoint by hand: 127.0.0.1 and the given port.
 * Returns 0 on success, -1 with errno set on failure. */
int sock_ipv4_loopback_bytes(uint16_t port, sock_ipv4_bytes *out);

/* Write our bytes into a sockaddr_in the kernel will accept.
 * The one exception is the family field, which POSIX stores in host byte
 * order -- it is a number, not a byte string. */
int sock_addr_apply(const sock_ipv4_bytes *b, struct sockaddr_in *out);

/* Read a sockaddr_in back out as bytes. */
int sock_addr_extract(const struct sockaddr_in *in, sock_ipv4_bytes *out);

/* The port number carried by two network-order bytes. */
uint16_t sock_port_from_bytes(const uint8_t port[2]);

/* Create a listening TCP socket on 127.0.0.1.
 * port == 0 asks the kernel to pick a free port; the chosen port comes back
 * in out->port. Returns 0 on success, -1 with errno set on failure. */
int sock_listen_loopback(sock_listener *out, uint16_t port, int backlog);

/* Accept one pending connection. Blocks until a connection is pending.
 * Returns a new descriptor, or -1 with errno set. */
int sock_accept(const sock_listener *l);

/* Connect to a TCP port on 127.0.0.1. Returns a new descriptor, or -1 with
 * errno set. */
int sock_connect_loopback(uint16_t port);

/* One read() / one write().
 * Returns the number of bytes transferred, 0 at end of stream, or -1 with
 * errno set. A short read or a short write is NOT reported as an error here:
 * it is stage 65.4's lesson, and this stage ships the naive single-call form
 * on purpose so that stage can make it honest. */
ssize_t sock_read_once(int fd, void *buf, size_t len);
ssize_t sock_write_once(int fd, const void *buf, size_t len);

/* Close a descriptor. Returns 0 on success, -1 with errno set (EBADF if the
 * descriptor was already closed). */
int sock_close(int fd);

/* ------------------------------------------------------------------ */
/* Stage 65.3 — the accept loop.                                       */
/* ------------------------------------------------------------------ */

/* A connection handler. It runs in the CHILD process for one connection,
 * with the connected descriptor, the 0-based connection number, and the
 * caller's context. Return 0 for success; anything else marks the child as
 * failed. */
typedef int (*sock_conn_handler)(int conn_fd, int index, void *ctx);

/* A forking server: one process accepts, one child per connection. */
typedef struct {
    sock_listener listener;   /* the listening socket the parent accepts on */
    int   connections;       /* connections accepted and forked */
    int   children_failed;   /* children whose handler failed */
    int   children_reaped;   /* children the parent has reaped */
    pid_t last_child;        /* pid of the most recently forked child */
} sock_server;

/* Create the listening socket for a server. port 0 asks the kernel to pick
 * a free port, which comes back in s->listener.port. */
int sock_server_start(sock_server *s, uint16_t port, int backlog);

/* Accept up to max_connections connections, one child process each.
 *
 * The parent accepts, forks, closes its copy of the connection, and reaps
 * any child that has finished -- without ever blocking the accept loop.
 * The child closes its copy of the LISTENING socket (it does not need it;
 * keeping it would stop the socket draining when the parent goes away),
 * runs the handler, closes the connection, and exits.
 *
 * Returns 0 once every connection has been served and every child reaped. */
int sock_server_serve(sock_server *s, sock_conn_handler handler, void *ctx,
                      int max_connections);

/* ------------------------------------------------------------------ */
/* Stage 65.4 — errors as protocol.                                    */
/* ------------------------------------------------------------------ */

/* What happened on the wire, as data.
 *
 * The naive form (sock_read_once / sock_write_once) hands back a number and
 * leaves the caller to remember that a number smaller than the one asked
 * for is not an error and not a success either -- it is a partial transfer
 * that nobody looks at. A short write is a silent truncation.
 *
 * This enum names the outcome instead. The five states a byte transfer can
 * be in are not variations of "error": they are different situations, and
 * the caller has to treat them differently. */
typedef enum {
    SOCK_IO_COMPLETE = 0,   /* everything asked for was transferred */
    SOCK_IO_PARTIAL,        /* some bytes moved, but not all -- NOT an error */
    SOCK_IO_EOF,            /* the peer closed: no more bytes will come */
    SOCK_IO_INTERRUPTED,    /* EINTR: a signal arrived; the call is retryable */
    SOCK_IO_WOULD_BLOCK,    /* EAGAIN: non-blocking socket, nothing to do now */
    SOCK_IO_ERROR           /* a real failure; err holds the errno value */
} sock_io_status;

/* The result of one transfer.
 *
 * err is a VALUE, not the global errno: it is captured at the moment of the
 * failure and travels with the result, so the caller cannot lose it by
 * calling something else before reading it. */
typedef struct {
    sock_io_status status;
    size_t          transferred;  /* bytes actually moved */
    int             err;          /* errno value when status is SOCK_IO_ERROR */
} sock_io_result;

/* The name of a status, for messages. */
const char *sock_io_status_name(sock_io_status st);

/* One read() / one write(), with the outcome as data.
 *
 * Returns 0 on success with *out filled; -1 only for API misuse (a NULL
 * result, or a NULL buffer with a non-zero length), with errno set.
 * A failure on the socket is NOT an API error: it is SOCK_IO_ERROR in *out. */
int sock_read_some(int fd, void *buf, size_t len, sock_io_result *out);
int sock_write_some(int fd, const void *buf, size_t len, sock_io_result *out);

/* Read or write until the whole request is satisfied.
 *
 * A loop, because a partial transfer is normal: the kernel gave you what it
 * had and you go back for the rest. The loop stops on anything it cannot
 * fix by trying again -- an error, a closed peer, or a non-blocking socket
 * with nothing available -- and the result says how far it got. */
int sock_read_exact(int fd, void *buf, size_t len, sock_io_result *out);
int sock_write_exact(int fd, const void *buf, size_t len, sock_io_result *out);

/* ------------------------------------------------------------------ */
/* Stage 65.5 -- non-blocking, multiplexed, framed.                    */
/* ------------------------------------------------------------------ */

/* Set or clear O_NONBLOCK on a descriptor.
 *
 * A non-blocking socket is the precondition for multiplexing: a server
 * that blocks in accept() or read() cannot be told to look at another
 * connection, so one slow client owns the whole process. */
int sock_set_nonblocking(int fd, int nonblocking);

/* The largest framed payload we will accept. */
#define SOCK_FRAME_MAX 4096
/* The buffer a frame parser needs: a 4-byte header plus a max payload. */
#define SOCK_FRAME_BUF (SOCK_FRAME_MAX + 8)
/* How many connections one multiplexer serves. */
#define SOCK_MAX_CONNS 32
/* The per-connection output buffer. */
#define SOCK_CONN_BUF 4096

/* A frame parser: bytes in, complete messages out.
 *
 * A stream has no message boundaries. One read() can return half a
 * message, one message, or three messages and the beginning of a fourth --
 * the kernel gives you bytes, not packets. Framing is the layer that
 * decides which, and you have to write it yourself: that is what "TCP is
 * a byte stream" means once you stop quoting it.
 *
 * The frame is a 4-byte big-endian length followed by that many payload
 * bytes. */
typedef struct {
    char   buf[SOCK_FRAME_BUF];  /* bytes received, not yet consumed */
    size_t len;                  /* bytes valid in buf */
    size_t off;                  /* consumed prefix of buf */
} sock_frame;

void sock_frame_init(sock_frame *f);

/* Add bytes to the parser's buffer. Returns 0, or -1 with errno set to
 * ENOBUFS if the buffer cannot hold them (a peer sending more than one
 * maximum-size message without letting us consume it). */
int sock_frame_feed(sock_frame *f, const void *data, size_t len);

/* Pull the next complete message.
 *
 * Returns 1 and fills out/len (out points into the parser's buffer and is
 * valid until the next feed), 0 if more bytes are needed, or -1 with errno
 * set to EMSGSIZE if the length prefix exceeds SOCK_FRAME_MAX -- a
 * protocol error the caller should answer by closing the connection. */
int sock_frame_next(sock_frame *f, const char **out, uint32_t *len);

/* Write a framed message (4-byte big-endian length + payload) into dst.
 * Returns the number of bytes written, or -1 if dst is too small. */
int sock_frame_encode(char *dst, size_t dst_len, const void *payload, uint32_t len);

/* One connection in a multiplexing server.
 *
 * The frame parser IS the input buffer: because we consume bytes only
 * when they turn out to belong to a whole message, the parser never has
 * to hold more than one message's worth, and the connection cannot grow
 * without bound. */
typedef struct {
    int            fd;
    int            in_use;
    int            closing;       /* the peer closed; drain and go */
    sock_frame     frame;         /* the input side: where the boundaries are */
    size_t         out_len;       /* bytes queued for the peer */
    size_t         out_off;       /* bytes already written */
    unsigned long  messages_in;
    unsigned long  bytes_out;
    char           out_buf[SOCK_CONN_BUF];
} sock_conn;

/* A message handler. It is called once per complete framed message and
 * may queue a reply with sock_conn_queue_frame(). */
typedef int (*sock_msg_handler)(sock_conn *c, const char *msg, uint32_t len, void *ctx);

/* A multiplexing server: one process, many connections, no fork. */
typedef struct {
    sock_listener listener;
    sock_conn     conns[SOCK_MAX_CONNS];
    int           nconns;
    unsigned long accepted;
    unsigned long messages;
} sock_mux;

/* Start a non-blocking listening socket. port 0 asks the kernel to pick. */
int sock_mux_start(sock_mux *m, uint16_t port, int backlog);

/* Accept every pending connection (non-blocking). Returns how many were
 * accepted. */
int sock_mux_accept(sock_mux *m);

/* Queue bytes for this connection. */
int sock_conn_queue(sock_conn *c, const void *data, size_t len);

/* Queue a framed message for this connection. */
int sock_conn_queue_frame(sock_conn *c, const void *payload, uint32_t len);

/* Wait up to timeout_ms for activity, then service it: accept new
 * connections, read what is readable, hand complete framed messages to the
 * handler, and write out whatever is queued.
 *
 * Returns the number of events serviced, or -1 with errno set. */
int sock_mux_poll(sock_mux *m, sock_msg_handler handler, void *ctx, int timeout_ms);

#endif /* SOCKETS_H */
