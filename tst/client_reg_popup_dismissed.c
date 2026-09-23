// A dismissed xdg_popup stays dismissed (xdg-shell: a dismissed popup is
// unmapped, and the client should destroy it). A red window; the first press
// on it opens a grab popup (green) with a plain child popup (blue). Modes:
//   refused  the grab is asked for on a serial that is not the press's, so
//            it is refused with popup_done; the client commits the popup's
//            buffer anyway, which must not map it;
//   outside  the scenario clicks outside: both popups get popup_done, and a
//            fresh buffer committed to each afterwards maps neither;
//   nested   after the outside click, a grabbing child is opened on the
//            dismissed popup: it is dismissed at once, not a protocol error.
// The scenario holds the button until the client says it asked for the grab.
// Stages print their outcome; the scenario checks the compositor's side.

#include "wl_util.h"

struct pop {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_popup* popup;
    uint32_t color;
    int configured, attached, done;
};

static struct wl_toplevel_ctx top;
static struct pop grab, child, late;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)p;
    ((struct pop*)d)->done = 1;
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct pop* pp = d;
    xdg_surface_ack_configure(xs, serial);
    pp->configured = 1;
    if (!pp->attached) {
        wl_surface_attach(pp->surface, wl_solid(60, 40, pp->color), 0, 0);
        wl_surface_commit(pp->surface);
        pp->attached = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void open_popup(struct pop* pp, struct xdg_surface* parent, uint32_t color, int grab_it, uint32_t serial, int dx) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 60, 40);
    xdg_positioner_set_anchor_rect(pos, 10 + dx, 10, 20, 20);
    pp->color = color;
    pp->surface = wl_compositor_create_surface(wl_comp);
    pp->xs = xdg_wm_base_get_xdg_surface(wl_wm, pp->surface);
    xdg_surface_add_listener(pp->xs, &xs_listener, pp);
    pp->popup = xdg_surface_get_popup(pp->xs, parent, pos);
    xdg_popup_add_listener(pp->popup, &popup_listener, pp);
    if (grab_it) {
        xdg_popup_grab(pp->popup, wl_seat_g, serial);
    }
    xdg_positioner_destroy(pos);
    wl_surface_commit(pp->surface);
}

// a fresh buffer on a popup that was dismissed, as a client that has not
// seen popup_done yet would send
static void recommit(struct pop* pp) {
    wl_surface_attach(pp->surface, wl_solid(60, 40, pp->color), 0, 0);
    wl_surface_damage(pp->surface, 0, 0, 60, 40);
    wl_surface_commit(pp->surface);
}

static void wait_go(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);
    printf("stage %s\n", name);
    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "disconnected at stage %s\n", name);
            exit(1);
        }
        usleep(20000);
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (argc < 2 || wl_boot() || !wl_ptr) return 2;
    const char* mode = argv[1];

    wl_make_toplevel(&top, "popup-dismissed", 300, 200, 0xFFFF0000);
    printf("mapped\n");
    while (!(wlp_button_count > 0 && wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) && wl_display_dispatch(wl_dpy) != -1) {
    }
    uint32_t press = wlp_button_serial;

    if (!strcmp(mode, "refused")) {
        open_popup(&grab, top.xs, 0xFF00FF00, 1, press + 1, 0);
        while ((!grab.done || !grab.attached) && wl_display_dispatch(wl_dpy) != -1) {
        }
        printf("grab asked\n");
        recommit(&grab);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        wait_go("refused");
        return 0;
    }

    open_popup(&grab, top.xs, 0xFF00FF00, 1, press, 0);
    while (!grab.attached && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
    open_popup(&child, grab.xs, 0xFF0000FF, 0, 0, 40);
    while (!child.attached && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
    printf("grab asked\n");
    wait_go("open");

    while ((!grab.done || !child.done) && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("both done\n");

    if (!strcmp(mode, "outside")) {
        recommit(&child);
        recommit(&grab);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        wait_go("recommitted");
        return 0;
    }

    // a grabbing child of the dismissed popup, on the old press's serial
    open_popup(&late, grab.xs, 0xFFFFFF00, 1, press, 0);
    while (!late.done && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "a grabbing child of a dismissed popup was a protocol error\n");
        return 1;
    }
    wait_go("nested");
    return 0;
}
