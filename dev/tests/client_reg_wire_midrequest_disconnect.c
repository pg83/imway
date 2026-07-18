/* Set up registry, surface and frame callback like a healthy client, then
 * close the socket in the middle of a request: only the first word of a
 * header goes out before the hard exit. */
#include "wl_util.h"

#include <sys/socket.h>

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    wl_surface_frame(surface);
    wl_display_flush(wl_dpy);

    uint32_t word = wl_proxy_get_id((struct wl_proxy*)surface);
    int fd = wl_display_get_fd(wl_dpy);
    if (send(fd, &word, sizeof(word), MSG_NOSIGNAL) != (ssize_t)sizeof(word)) return 2;
    return 0;
}
