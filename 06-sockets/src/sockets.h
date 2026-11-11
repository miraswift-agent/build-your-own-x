/*
 * Stage 65.1 — the fd lifecycle.
 *
 * A socket is a file descriptor: an integer the kernel hands us that stands
 * for a kernel object. Everything in this component is a consequence of that
 * one fact, so Stage 65.1 is deliberately thin -- it is the vocabulary the
 * later stages use.
 *
 * Scope note: the address helpers here use inet_pton/htons so that this
 * stage's lesson is the descriptor and not the byte order. Stage 65.2
 * replaces them with hand-packed bytes.
 */

#ifndef SOCKETS_H
#define SOCKETS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* A listening TCP socket bound to loopback. */
typedef struct {
    int      listen_fd;  /* the listening descriptor */
    uint16_t port;       /* the port actually bound (resolved via getsockname) */
} sock_listener;

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

#endif /* SOCKETS_H */
