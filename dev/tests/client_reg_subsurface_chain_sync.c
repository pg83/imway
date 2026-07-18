/* PLAN depth #1: chains of sync subsurfaces at depths 64, 256 and 1024. Each
 * commits bottom-up, gets a frame callback on the deepest surface, and is torn
 * down top-down or bottom-up. Every visible surface needs its own texture
 * descriptor, so a deep tree used to exhaust imgui's fixed descriptor pool and
 * crash the compositor; VkTexturePool now grows on demand, so 1024 must work. */
#include "wl_util.h"

static int frame_done;

static void done(void* d, struct wl_callback* callback, uint32_t time) {
    (void)d; (void)time;
    frame_done = 1;
    wl_callback_destroy(callback);
}
static const struct wl_callback_listener frame_listener = {done};

#define MAX_DEPTH 1024

static struct wl_surface* chain_surface[MAX_DEPTH];
static struct wl_subsurface* chain_sub[MAX_DEPTH];

static int run_chain(struct wl_toplevel_ctx* top, int depth, int destroy_topdown) {
    for (int i = 0; i < depth; i++) {
        chain_surface[i] = wl_compositor_create_surface(wl_comp);
        chain_sub[i] = wl_subcompositor_get_subsurface(
            wl_subcomp, chain_surface[i], i ? chain_surface[i - 1] : top->surface);
        wl_subsurface_set_sync(chain_sub[i]);
    }

    /* bottom-up: deepest first; the frame callback rides the deepest */
    frame_done = 0;
    for (int i = depth - 1; i >= 0; i--) {
        if (i == depth - 1) wl_surface_frame(chain_surface[i]);
        wl_surface_attach(chain_surface[i], wl_solid(4, 4, 0xff00ff00), 0, 0);
        wl_surface_commit(chain_surface[i]);
    }
    wl_surface_commit(top->surface); /* applies the whole sync subtree */
    wl_callback_add_listener(wl_surface_frame(top->surface), &frame_listener, NULL);
    wl_surface_commit(top->surface);

    while (!frame_done && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!frame_done) {
        fprintf(stderr, "depth %d: no frame (errno=%d)\n", depth, wl_display_get_error(wl_dpy));
        return 1;
    }

    if (destroy_topdown) {
        for (int i = 0; i < depth; i++) {
            wl_subsurface_destroy(chain_sub[i]);
            wl_surface_destroy(chain_surface[i]);
        }
    } else {
        for (int i = depth - 1; i >= 0; i--) {
            wl_subsurface_destroy(chain_sub[i]);
            wl_surface_destroy(chain_surface[i]);
        }
    }
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}

int main(void) {
    alarm(60);
    if (wl_boot() || !wl_subcomp) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "chain-sync", 64, 64, 0xffff0000);

    if (run_chain(&top, 64, 1)) return 1;
    if (run_chain(&top, 256, 0)) return 1;
    if (run_chain(&top, 1024, 1)) return 1;
    return 0;
}
