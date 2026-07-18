#define _GNU_SOURCE
#include <sys/mman.h>

#include "wire_error.inc"

int main(void) {
    alarm(10);
    wire_boot();
    int fd = memfd_create("unexpected-wayland-fd", 0);
    uint32_t sync_msg[] = {1, 12u << 16, 2};
    char control[CMSG_SPACE(sizeof(fd))];
    struct iovec iov = {sync_msg, sizeof(sync_msg)};
    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
    if (sendmsg(wire_fd, &msg, MSG_NOSIGNAL) != (ssize_t)sizeof(sync_msg)) return 2;
    close(fd);
    uint32_t bad[] = {1, (8u << 16) | 0xffffu};
    wire_send(bad, 2);
    return wire_wait_closed();
}
