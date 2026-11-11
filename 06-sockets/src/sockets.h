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

#endif /* SOCKETS_H */
