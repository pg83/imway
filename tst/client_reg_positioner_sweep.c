/* Every positioner anchor and gravity, constrained. A fullscreen parent
 * hosts one popup at a time; each case anchors it so the placement escapes
 * the work area, which forces the compositor down a different arm of its
 * flip/slide/resize adjustment. The scenario reads the placed rectangle from
 * the state dump between cases and taps KEY_1 to advance. */

#include "wl_util.h"

#include <linux/input-event-codes.h>

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int cur_w, cur_h;
static int pend_w, pend_h;
static int fullscreen_seen, committed;

static struct wl_surface* popup_surface;
static struct xdg_surface* popup_xs;
static struct xdg_popup* popup;
static int popup_w, popup_h;
static int popup_committed;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t;
    uint32_t* s;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_FULLSCREEN) fullscreen_seen = 1;
    }
    pend_w = w;
    pend_h = h;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (pend_w > 0 && pend_h > 0 && !committed) {
        cur_w = pend_w;
        cur_h = pend_h;
        wl_surface_attach(surface, wl_solid(cur_w, cur_h, 0xFF404040), 0, 0);
        wl_surface_damage(surface, 0, 0, cur_w, cur_h);
        wl_surface_commit(surface);
        committed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void popup_xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!popup_committed) {
        wl_surface_attach(popup_surface, wl_solid(popup_w, popup_h, 0xFFFFFF00), 0, 0);
        wl_surface_damage(popup_surface, 0, 0, popup_w, popup_h);
        wl_surface_commit(popup_surface);
        popup_committed = 1;
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p;
    /* the compositor's resize adjustment comes back as a smaller size */
    if (w > 0 && h > 0) {
        popup_w = w;
        popup_h = h;
    }
    printf("configure %d,%d %dx%d\n", x, y, w, h);
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_repositioned};

struct Case {
    const char* name;
    int w, h;          /* requested popup size */
    int ax, ay, aw, ah;/* anchor rect, negative x/y means "from the far edge" */
    uint32_t anchor;
    uint32_t gravity;
    uint32_t adjust;
};

#define FLIP (XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y)
#define SLIDE (XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y)
#define RESIZE (XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y)

/* -1 in ax/ay: the anchor rect hugs the right/bottom edge; -2: the centre */
static const struct Case cases[] = {
    {"top",          200, 150, -2,  0, 20, 20, XDG_POSITIONER_ANCHOR_TOP,          XDG_POSITIONER_GRAVITY_TOP,          FLIP},
    {"bottom",       200, 150, -2, -1, 20, 20, XDG_POSITIONER_ANCHOR_BOTTOM,       XDG_POSITIONER_GRAVITY_BOTTOM,       FLIP},
    {"left",         200, 150,  0, -2, 20, 20, XDG_POSITIONER_ANCHOR_LEFT,         XDG_POSITIONER_GRAVITY_LEFT,         FLIP},
    {"right",        200, 150, -1, -2, 20, 20, XDG_POSITIONER_ANCHOR_RIGHT,        XDG_POSITIONER_GRAVITY_RIGHT,        FLIP},
    {"top-left",     200, 150,  0,  0, 20, 20, XDG_POSITIONER_ANCHOR_TOP_LEFT,     XDG_POSITIONER_GRAVITY_TOP_LEFT,     FLIP},
    {"bottom-left",  200, 150,  0, -1, 20, 20, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT,  XDG_POSITIONER_GRAVITY_BOTTOM_LEFT,  FLIP},
    {"top-right",    200, 150, -1,  0, 20, 20, XDG_POSITIONER_ANCHOR_TOP_RIGHT,    XDG_POSITIONER_GRAVITY_TOP_RIGHT,    FLIP},
    {"bottom-right", 200, 150, -1, -1, 20, 20, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT, FLIP},
    /* no anchor and no gravity: both switches take their default arm */
    {"none",         200, 150, -2, -2, 20, 20, XDG_POSITIONER_ANCHOR_NONE,         XDG_POSITIONER_GRAVITY_NONE,         SLIDE},
    /* a flip that would not fit either, so the slide has to finish the job */
    {"flip-then-slide-x", 700, 150, -2, -2, 20, 20, XDG_POSITIONER_ANCHOR_RIGHT,   XDG_POSITIONER_GRAVITY_RIGHT,        FLIP | SLIDE},
    {"flip-then-slide-y", 200, 700, -2, -2, 20, 20, XDG_POSITIONER_ANCHOR_BOTTOM,  XDG_POSITIONER_GRAVITY_BOTTOM,       FLIP | SLIDE},
    /* larger than the output on both axes: only a resize can place it */
    {"resize",      1400, 900, -2, -2, 20, 20, XDG_POSITIONER_ANCHOR_NONE,         XDG_POSITIONER_GRAVITY_NONE,         RESIZE},
    /* off the left edge: the slide pulls it back in */
    {"slide-left",   200, 150,  0, -2, 20, 20, XDG_POSITIONER_ANCHOR_LEFT,         XDG_POSITIONER_GRAVITY_LEFT,         SLIDE},
    /* wider or taller than the output: no slide can place it, the resize does */
    {"too-wide",    1400, 150, -2, -2, 20, 20, XDG_POSITIONER_ANCHOR_NONE,         XDG_POSITIONER_GRAVITY_NONE,         SLIDE | RESIZE},
    {"too-tall",     200, 900, -2, -2, 20, 20, XDG_POSITIONER_ANCHOR_NONE,         XDG_POSITIONER_GRAVITY_NONE,         SLIDE | RESIZE},
    /* partly above the top: the resize keeps the part that is on screen */
    {"resize-top",   200, 150, -2, 50, 20, 20, XDG_POSITIONER_ANCHOR_TOP,          XDG_POSITIONER_GRAVITY_TOP,          RESIZE},
    /* wholly beside the output: nothing of it is left to keep, so the
     * resize spans the whole axis */
    {"resize-left",  200, 150,  0, -2, 20, 20, XDG_POSITIONER_ANCHOR_LEFT,         XDG_POSITIONER_GRAVITY_LEFT,         RESIZE},
    {"resize-above", 200, 150, -2,  0, 20, 20, XDG_POSITIONER_ANCHOR_TOP,          XDG_POSITIONER_GRAVITY_TOP,          RESIZE},
    /* partly past the far edges: the resize keeps the part on screen */
    {"resize-bottom", 200, 150, -2, 750, 20, 20, XDG_POSITIONER_ANCHOR_BOTTOM,      XDG_POSITIONER_GRAVITY_BOTTOM,       RESIZE},
    {"resize-right", 200, 150, 1200, -2, 20, 20, XDG_POSITIONER_ANCHOR_RIGHT,       XDG_POSITIONER_GRAVITY_RIGHT,        RESIZE},
};

static int anchor_coord(int value, int extent, int rect) {
    if (value == -1) return extent - rect;
    if (value == -2) return extent / 2;
    return value;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(120);
    if (wl_boot() || !wl_kbd) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "positioner-sweep");
    xdg_toplevel_set_app_id(tl, "positioner-sweep");
    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_surface_commit(surface);

    while ((!committed || !fullscreen_seen) && wl_display_dispatch(wl_dpy) != -1) {
    }

    wlk_watch_key = KEY_1;
    printf("sweep ready %dx%d\n", cur_w, cur_h);

    size_t count = sizeof(cases) / sizeof(cases[0]);

    for (size_t i = 0; i < count; i++) {
        const struct Case* c = &cases[i];
        struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

        popup_w = c->w;
        popup_h = c->h;
        popup_committed = 0;

        xdg_positioner_set_size(pos, c->w, c->h);
        xdg_positioner_set_anchor_rect(pos, anchor_coord(c->ax, cur_w, c->aw),
                                       anchor_coord(c->ay, cur_h, c->ah), c->aw, c->ah);
        xdg_positioner_set_anchor(pos, c->anchor);
        xdg_positioner_set_gravity(pos, c->gravity);
        xdg_positioner_set_constraint_adjustment(pos, c->adjust);

        popup_surface = wl_compositor_create_surface(wl_comp);
        popup_xs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
        xdg_surface_add_listener(popup_xs, &popup_xs_listener, NULL);
        popup = xdg_surface_get_popup(popup_xs, xs, pos);
        xdg_popup_add_listener(popup, &popup_listener, NULL);
        wl_surface_commit(popup_surface);
        xdg_positioner_destroy(pos);

        while (!popup_committed && wl_display_dispatch(wl_dpy) != -1) {
        }

        wl_display_roundtrip(wl_dpy);
        printf("case %s mapped\n", c->name);

        /* the helper counts press and release alike, and the scenario taps
         * the key once per case */
        int want = ((int)i + 1) * 2;

        while (wlk_watch_hits < want && wl_display_dispatch(wl_dpy) != -1) {
        }

        xdg_popup_destroy(popup);
        xdg_surface_destroy(popup_xs);
        wl_surface_destroy(popup_surface);
        popup = NULL;
        popup_xs = NULL;
        popup_surface = NULL;
        wl_display_roundtrip(wl_dpy);
    }

    printf("sweep done\n");

    return 0;
}
