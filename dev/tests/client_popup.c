// Popup test client: red 300x200 toplevel; after it maps — a yellow 120x90
// grab popup at the bottom-left edge of the anchor rect (20,20,60,20). On
// popup_done prints "popup done" and tears the popup down (for the
// dismiss-on-click-outside test).

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

static struct wl_display* display;
static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wm_base;
static struct wl_seat* seat;
static struct wl_pointer* pointer;
static struct wl_surface* surface;
static struct wl_surface* popup_surface;
static struct xdg_popup* popup;
static int drawn, popup_drawn;
static struct xdg_surface* toplevel_xs;

static void registry_global(void* data, struct wl_registry* reg, uint32_t name,
                            const char* iface, uint32_t version) {
    (void)data;
    (void)version;
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 3);
    else if (!strcmp(iface, wl_seat_interface.name))
        seat = wl_registry_bind(reg, name, &wl_seat_interface, 5);
}

static void registry_global_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}

static const struct wl_registry_listener registry_listener = {registry_global,
                                                              registry_global_remove};

static void wm_base_ping(void* d, struct xdg_wm_base* wb, uint32_t serial) {
    (void)d;
    xdg_wm_base_pong(wb, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {wm_base_ping};

static struct wl_buffer* solid_buffer(int w, int h, uint32_t argb) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("popup-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) px[i] = argb;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                                      WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

// --- popup ---

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d;
    (void)p;
    printf("client_popup: popup configure %dx%d @ (%d,%d)\n", w, h, x, y);
}

static void popup_done_ev(void* d, struct xdg_popup* p) {
    (void)d;
    printf("client_popup: popup done\n");
    fflush(stdout);
    xdg_popup_destroy(p);
    wl_surface_destroy(popup_surface);
    popup = NULL;
}

static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d;
    (void)p;
    (void)token;
}

static const struct xdg_popup_listener popup_listener = {
    .configure = popup_configure,
    .popup_done = popup_done_ev,
    .repositioned = popup_repositioned,
};

static void popup_xdg_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!popup_drawn) {
        wl_surface_attach(popup_surface, solid_buffer(120, 90, 0xFFFFFF00), 0, 0); // yellow
        wl_surface_commit(popup_surface);
        popup_drawn = 1;
        printf("client_popup: popup committed\n");
    }
}
static const struct xdg_surface_listener popup_xdg_listener = {popup_xdg_configure};

static void open_popup(struct xdg_surface* parent_xs, uint32_t serial) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wm_base);
    xdg_positioner_set_size(pos, 120, 90);
    xdg_positioner_set_anchor_rect(pos, 20, 20, 60, 20);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_offset(pos, 5, 5);

    popup_surface = wl_compositor_create_surface(compositor);
    struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wm_base, popup_surface);
    xdg_surface_add_listener(pxs, &popup_xdg_listener, NULL);
    popup = xdg_surface_get_popup(pxs, parent_xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    if (seat) xdg_popup_grab(popup, seat, serial);
    xdg_positioner_destroy(pos);
    wl_surface_commit(popup_surface); // no buffer → wait for configure
}

// --- pointer / toplevel ---

static void pointer_enter(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s,
                          wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)serial; (void)s; (void)x; (void)y;
}
static void pointer_leave(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s) {
    (void)d; (void)p; (void)serial; (void)s;
}
static void pointer_motion(void* d, struct wl_pointer* p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)time; (void)x; (void)y;
}
static void pointer_button(void* d, struct wl_pointer* p, uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state) {
    (void)d; (void)p; (void)time; (void)button;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED && drawn && !popup)
        open_popup(toplevel_xs, serial);
}
static void pointer_axis(void* d, struct wl_pointer* p, uint32_t time, uint32_t axis,
                         wl_fixed_t value) {
    (void)d; (void)p; (void)time; (void)axis; (void)value;
}
static void pointer_frame(void* d, struct wl_pointer* p) { (void)d; (void)p; }
static void pointer_axis_source(void* d, struct wl_pointer* p, uint32_t source) {
    (void)d; (void)p; (void)source;
}
static void pointer_axis_stop(void* d, struct wl_pointer* p, uint32_t time, uint32_t axis) {
    (void)d; (void)p; (void)time; (void)axis;
}
static void pointer_axis_discrete(void* d, struct wl_pointer* p, uint32_t axis, int32_t discrete) {
    (void)d; (void)p; (void)axis; (void)discrete;
}
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
    .button = pointer_button, .axis = pointer_axis, .frame = pointer_frame,
    .axis_source = pointer_axis_source, .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void xdg_surface_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!drawn) {
        wl_surface_attach(surface, solid_buffer(300, 200, 0xFFFF0000), 0, 0);
        wl_surface_commit(surface);
        drawn = 1;
        printf("client_popup: toplevel committed\n");
        printf("client_popup: ready for grab\n");
    }
}
static const struct xdg_surface_listener xdg_surface_listener = {xdg_surface_configure};

static void toplevel_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                               struct wl_array* states) {
    (void)d;
    (void)t;
    (void)w;
    (void)h;
    (void)states;
}
static void toplevel_close(void* d, struct xdg_toplevel* t) {
    (void)d;
    (void)t;
    exit(0);
}
static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "client_popup: cannot connect\n");
        return 1;
    }
    struct wl_registry* reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !wm_base) {
        fprintf(stderr, "client_popup: missing globals\n");
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    if (seat) {
        pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &pointer_listener, NULL);
    }

    surface = wl_compositor_create_surface(compositor);
    toplevel_xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(toplevel_xs, &xdg_surface_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(toplevel_xs);
    xdg_toplevel_add_listener(tl, &toplevel_listener, NULL);
    xdg_toplevel_set_title(tl, "client_popup");
    wl_surface_commit(surface);

    while (wl_display_dispatch(display) != -1) {
    }
    return 0;
}
