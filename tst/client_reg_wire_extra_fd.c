/* A valid wl_shm.create_pool with a second, unexpected fd riding the same
 * SCM_RIGHTS. The pool must work (no protocol error) and the stray fd must
 * be closed by the server when this client goes away — the scenario loops
 * this client and compares the compositor's fd count. */
#include "wl_util.h"

#include <sys/socket.h>

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    wl_display_flush(wl_dpy);

    int pool_fd = memfd_create("extra-fd-pool", 0);
    int extra_fd = memfd_create("extra-fd-noise", 0);
    if (pool_fd < 0 || extra_fd < 0 || ftruncate(pool_fd, 4096) < 0) return 2;

    /* allocate the new_id client-side without sending anything, so the raw
     * message below carries an id the server will accept */
    struct wl_proxy* pool =
        wl_proxy_create((struct wl_proxy*)wl_shm_g, &wl_shm_pool_interface);
    uint32_t msg[] = {wl_proxy_get_id((struct wl_proxy*)wl_shm_g), 16u << 16,
                      wl_proxy_get_id(pool), 4096};
    int fds[2] = {pool_fd, extra_fd};
    char control[CMSG_SPACE(sizeof(fds))];
    struct iovec iov = {msg, sizeof(msg)};
    struct msghdr hdr = {0};
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = control;
    hdr.msg_controllen = sizeof(control);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&hdr);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fds));
    memcpy(CMSG_DATA(cmsg), fds, sizeof(fds));
    int sock = wl_display_get_fd(wl_dpy);
    if (sendmsg(sock, &hdr, MSG_NOSIGNAL) != (ssize_t)sizeof(msg)) return 2;
    close(pool_fd);
    close(extra_fd);

    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "unexpected error after extra fd\n");
        return 1;
    }
    wl_display_disconnect(wl_dpy);
    return 0;
}
