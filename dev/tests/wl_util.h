// Shared boilerplate for the regression test clients (client_reg_*.c). Each
// client includes this once and gets its own copy of the globals; the header
// carries the registry/seat plumbing, an shm buffer helper, and small records
// of the last keyboard/pointer events so a scenario can assert on them.
//
// Define REG_DDM_VERSION before including to pin the data_device_manager bind
// version (default 3); the cancelled-v1 test wants 1.

#ifndef WL_UTIL_H
#define WL_UTIL_H

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

#ifndef REG_DDM_VERSION
#define REG_DDM_VERSION 3
#endif

__attribute__((unused)) static struct wl_display* wl_dpy;
__attribute__((unused)) static struct wl_compositor* wl_comp;
__attribute__((unused)) static struct wl_subcompositor* wl_subcomp;
__attribute__((unused)) static struct wl_shm* wl_shm_g;
__attribute__((unused)) static struct xdg_wm_base* wl_wm;
__attribute__((unused)) static struct wl_seat* wl_seat_g;
__attribute__((unused)) static struct wl_data_device_manager* wl_ddm;
__attribute__((unused)) static struct wl_keyboard* wl_kbd;
__attribute__((unused)) static struct wl_pointer* wl_ptr;

// last keyboard events, for scenarios that assert on focus / grab serials
__attribute__((unused)) static int wlk_keymap_fd = -1;
__attribute__((unused)) static uint32_t wlk_keymap_size;
__attribute__((unused)) static uint32_t wlk_key_serial;   // last wl_keyboard.key
__attribute__((unused)) static uint32_t wlk_last_key;     // its keycode
__attribute__((unused)) static uint32_t wlk_enter_serial; // last wl_keyboard.enter
__attribute__((unused)) static int wlk_enters, wlk_leaves;
__attribute__((unused)) static struct wl_surface* wlk_focus;
// set wlk_watch_key to a keycode and wlk_watch_hits counts its key events —
// used to tell "the client received this key" from "a chord swallowed it"
__attribute__((unused)) static uint32_t wlk_watch_key = 0xffffffff;
__attribute__((unused)) static int wlk_watch_hits;
// modifiers + repeat_info
__attribute__((unused)) static uint32_t wlk_mods_depressed, wlk_mods_max_depressed;
__attribute__((unused)) static int wlk_mods_count, wlk_got_repeat;
__attribute__((unused)) static int32_t wlk_repeat_rate, wlk_repeat_delay;

// last pointer events
__attribute__((unused)) static uint32_t wlp_enter_serial;  // last wl_pointer.enter
__attribute__((unused)) static int wlp_enter_count;
__attribute__((unused)) static uint32_t wlp_button_serial; // last wl_pointer.button
__attribute__((unused)) static uint32_t wlp_button, wlp_button_state;
__attribute__((unused)) static int wlp_button_count, wlp_motion_count, wlp_axis_count;
__attribute__((unused)) static wl_fixed_t wlp_x, wlp_y, wlp_axis_value;
__attribute__((unused)) static uint32_t wlp_axis_which;
__attribute__((unused)) static struct wl_surface* wlp_focus;
// relative-pointer deltas
__attribute__((unused)) static int wlrel_count;
__attribute__((unused)) static double wlrel_dx, wlrel_dy;

// ---- keyboard ----

static void wlk_keymap(void* d, struct wl_keyboard* k, uint32_t format, int32_t fd, uint32_t size) {
    (void)d; (void)k; (void)format;
    wlk_keymap_fd = fd;
    wlk_keymap_size = size;
}
__attribute__((unused)) static uint32_t wlk_enter_keys[8]; // pressed keys carried by the last enter
__attribute__((unused)) static int wlk_enter_nkeys;

static void wlk_enter(void* d, struct wl_keyboard* k, uint32_t serial, struct wl_surface* s,
                      struct wl_array* keys) {
    (void)d; (void)k;
    wlk_enters++;
    wlk_enter_serial = serial;
    wlk_focus = s;
    wlk_enter_nkeys = 0;
    uint32_t* kc;
    wl_array_for_each(kc, keys) {
        if (wlk_enter_nkeys < 8) wlk_enter_keys[wlk_enter_nkeys++] = *kc;
    }
}
static void wlk_leave(void* d, struct wl_keyboard* k, uint32_t serial, struct wl_surface* s) {
    (void)d; (void)k; (void)serial; (void)s;
    wlk_leaves++;
    wlk_focus = NULL;
}
static void wlk_key(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t t, uint32_t key,
                    uint32_t state) {
    (void)d; (void)k; (void)t; (void)state;
    wlk_key_serial = serial;
    wlk_last_key = key;
    if (key == wlk_watch_key) wlk_watch_hits++;
}
static void wlk_mods(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t dep, uint32_t lat,
                     uint32_t lock, uint32_t grp) {
    (void)d; (void)k; (void)serial; (void)lat; (void)lock; (void)grp;
    wlk_mods_depressed = dep;
    if (dep > wlk_mods_max_depressed) wlk_mods_max_depressed = dep;
    wlk_mods_count++;
}
static void wlk_repeat(void* d, struct wl_keyboard* k, int32_t rate, int32_t delay) {
    (void)d; (void)k;
    wlk_repeat_rate = rate; wlk_repeat_delay = delay; wlk_got_repeat = 1;
}
static const struct wl_keyboard_listener wlk_listener = {
    .keymap = wlk_keymap, .enter = wlk_enter, .leave = wlk_leave,
    .key = wlk_key, .modifiers = wlk_mods, .repeat_info = wlk_repeat,
};

// ---- pointer ----

static void wlp_enter(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s,
                      wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p;
    wlp_enter_serial = serial;
    wlp_enter_count++;
    wlp_focus = s;
    wlp_x = x; // enter carries surface-local coords too
    wlp_y = y;
}
static void wlp_leave(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s) {
    (void)d; (void)p; (void)serial; (void)s;
    wlp_focus = NULL;
}
static void wlp_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t;
    wlp_x = x; wlp_y = y; wlp_motion_count++;
}
static void wlp_button_ev(void* d, struct wl_pointer* p, uint32_t serial, uint32_t t,
                          uint32_t button, uint32_t state) {
    (void)d; (void)p; (void)t;
    wlp_button_serial = serial;
    wlp_button = button;
    wlp_button_state = state;
    wlp_button_count++;
}
static void wlp_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)d; (void)p; (void)t;
    wlp_axis_which = a; wlp_axis_value = v; wlp_axis_count++;
}
static void wlp_frame(void* d, struct wl_pointer* p) { (void)d; (void)p; }
static void wlp_axis_src(void* d, struct wl_pointer* p, uint32_t s) { (void)d; (void)p; (void)s; }
static void wlp_axis_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t a) {
    (void)d; (void)p; (void)t; (void)a;
}
static void wlp_axis_disc(void* d, struct wl_pointer* p, uint32_t a, int32_t v) {
    (void)d; (void)p; (void)a; (void)v;
}
static const struct wl_pointer_listener wlp_listener = {
    .enter = wlp_enter, .leave = wlp_leave, .motion = wlp_motion, .button = wlp_button_ev,
    .axis = wlp_axis, .frame = wlp_frame, .axis_source = wlp_axis_src,
    .axis_stop = wlp_axis_stop, .axis_discrete = wlp_axis_disc,
};

// relative-pointer deltas are recorded by wlrel_* above; the client that binds
// the relative pointer wires it to its own listener (the protocol header must
// be included there, after wl_util.h)

// ---- seat ----

static void wl_seat_caps(void* d, struct wl_seat* s, uint32_t caps) {
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl_kbd) {
        wl_kbd = wl_seat_get_keyboard(s);
        wl_keyboard_add_listener(wl_kbd, &wlk_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wl_ptr) {
        wl_ptr = wl_seat_get_pointer(s);
        wl_pointer_add_listener(wl_ptr, &wlp_listener, NULL);
    }
}
static void wl_seat_name(void* d, struct wl_seat* s, const char* n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener wl_seat_listener = {wl_seat_caps, wl_seat_name};

// ---- registry ----

static void wl_reg_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                          uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wl_compositor_interface.name))
        wl_comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_subcompositor_interface.name))
        wl_subcomp = wl_registry_bind(r, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(iface, wl_shm_interface.name))
        wl_shm_g = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name))
        wl_wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 3);
    else if (!strcmp(iface, wl_seat_interface.name)) {
        wl_seat_g = wl_registry_bind(r, name, &wl_seat_interface, 5);
        wl_seat_add_listener(wl_seat_g, &wl_seat_listener, NULL);
    } else if (!strcmp(iface, wl_data_device_manager_interface.name))
        wl_ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, REG_DDM_VERSION);
}
static void wl_reg_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener wl_reg_listener = {wl_reg_global, wl_reg_remove};

static void wl_wm_ping(void* d, struct xdg_wm_base* wm, uint32_t serial) {
    (void)d;
    xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wl_wm_listener = {wl_wm_ping};

// ---- helpers ----

// connect, bind globals, and run two roundtrips (globals, then seat caps).
// Returns 0 on success.
static int wl_boot(void) {
    wl_dpy = wl_display_connect(NULL);
    if (!wl_dpy) {
        fprintf(stderr, "cannot connect to compositor\n");
        return 1;
    }
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &wl_reg_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!wl_comp || !wl_shm_g || !wl_wm) {
        fprintf(stderr, "missing core globals (compositor=%p shm=%p wm_base=%p)\n",
                (void*)wl_comp, (void*)wl_shm_g, (void*)wl_wm);
        return 1;
    }
    xdg_wm_base_add_listener(wl_wm, &wl_wm_listener, NULL);
    wl_display_roundtrip(wl_dpy); // let seat capabilities arrive
    return 0;
}

static struct wl_buffer* wl_solid(int w, int h, uint32_t argb) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("reg-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) px[i] = argb;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

// A mapped toplevel of the given solid color. Blocks until it is on screen
// (the initial configure acked and the buffer committed). Returns its
// xdg_surface; *out_surface receives the wl_surface.
struct wl_toplevel_ctx {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_toplevel* tl;
    uint32_t color;
    int w, h;
    int committed;
};

static void wl_tl_xdg_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct wl_toplevel_ctx* c = d;
    xdg_surface_ack_configure(xs, serial);
    if (!c->committed) {
        wl_surface_attach(c->surface, wl_solid(c->w, c->h, c->color), 0, 0);
        wl_surface_damage(c->surface, 0, 0, c->w, c->h);
        wl_surface_commit(c->surface);
        c->committed = 1;
    }
}
static const struct xdg_surface_listener wl_tl_xdg_listener = {wl_tl_xdg_configure};

static void wl_tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                            struct wl_array* s) {
    (void)d; (void)t; (void)w; (void)h; (void)s;
}
static void wl_tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener wl_tl_listener = {wl_tl_configure, wl_tl_close};

static void wl_make_toplevel(struct wl_toplevel_ctx* c, const char* title, int w, int h,
                             uint32_t color) {
    c->w = w;
    c->h = h;
    c->color = color;
    c->committed = 0;
    c->surface = wl_compositor_create_surface(wl_comp);
    c->xs = xdg_wm_base_get_xdg_surface(wl_wm, c->surface);
    xdg_surface_add_listener(c->xs, &wl_tl_xdg_listener, c);
    c->tl = xdg_surface_get_toplevel(c->xs);
    xdg_toplevel_add_listener(c->tl, &wl_tl_listener, c);
    xdg_toplevel_set_title(c->tl, title);
    xdg_toplevel_set_app_id(c->tl, title);
    wl_surface_commit(c->surface);
    while (!c->committed && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
}

#endif
