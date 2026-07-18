#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <wayland-client.h>

static struct wl_display* wire_display;
static int wire_fd;

static void wire_boot(void) {
    wire_display = wl_display_connect(NULL);
    if (!wire_display) exit(2);
    wire_fd = wl_display_get_fd(wire_display);
}

static void wire_send(const uint32_t* words, size_t count) {
    size_t bytes = count * sizeof(*words);
    if (send(wire_fd, words, bytes, MSG_NOSIGNAL) != (ssize_t)bytes) exit(2);
}

static int wire_wait_closed(void) {
    while (wl_display_dispatch(wire_display) >= 0) {
    }
    return 0;
}
