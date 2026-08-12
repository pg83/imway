/* PLAN buffer lifetime #7: destroy a surface that still owes frame callbacks
 * and presentation feedback. The compositor must drop both without touching
 * freed state on its next frames. */
#include "wl_util.h"

#include <presentation-time-client-protocol.h>

static struct wp_presentation* pres;

static void pres_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_presentation_interface.name))
        pres = wl_registry_bind(r, name, &wp_presentation_interface, 1);
}
static void pres_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener pres_listener = {pres_global, pres_remove};

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &pres_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!pres) {
        fprintf(stderr, "no wp_presentation\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "destroy-pending", 64, 64, 0xffff0000);

    for (int i = 0; i < 3; i++) wl_surface_frame(top.surface);
    wp_presentation_feedback(pres, top.surface);
    wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);

    xdg_toplevel_destroy(top.tl);
    xdg_surface_destroy(top.xs);
    wl_surface_destroy(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    usleep(100000); /* a few frame ticks with the surface gone */
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
