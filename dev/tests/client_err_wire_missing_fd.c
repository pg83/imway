/* wl_shm.create_pool carries its fd in ancillary data; send the message
 * body without any fd. The server must kill this client, not stall or die. */
#include "wl_util.h"

#include <sys/socket.h>

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    wl_display_flush(wl_dpy);
    struct wl_proxy* pool =
        wl_proxy_create((struct wl_proxy*)wl_shm_g, &wl_shm_pool_interface);
    uint32_t msg[] = {wl_proxy_get_id((struct wl_proxy*)wl_shm_g), 16u << 16,
                      wl_proxy_get_id(pool), 4096};
    int fd = wl_display_get_fd(wl_dpy);
    if (send(fd, msg, sizeof(msg), MSG_NOSIGNAL) != (ssize_t)sizeof(msg)) return 2;
    while (wl_display_dispatch(wl_dpy) >= 0) {
    }
    return 0;
}
