/*
 * Stage 65.1 — the fd lifecycle.
 *
 * Nothing here is clever. That is the point: the descriptor is the whole
 * interface, and every mistake that matters later in this arc comes from
 * forgetting that the kernel, not the process, owns the connection.
 */

#include "sockets.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Loopback address, in network byte order.
 * Stage 65.2 replaces the library calls here with hand-packed bytes; the
 * lesson of 65.1 is the descriptor, not the byte order. */
static int loopback_addr(uint16_t port, struct sockaddr_in *addr)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr->sin_addr) != 1) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int sock_listen_loopback(sock_listener *out, uint16_t port, int backlog)
{
    struct sockaddr_in addr;
    struct sockaddr_in bound;
    socklen_t bound_len = sizeof(bound);
    int fd;
    int one = 1;

    if (out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (loopback_addr(port, &addr) != 0) {
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

    out->listen_fd = fd;
    out->port = ntohs(bound.sin_port);
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
    struct sockaddr_in addr;
    int fd;

    if (loopback_addr(port, &addr) != 0) {
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
