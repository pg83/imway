/* PLAN buffer lifetime #6: request presentation feedback, then drop the
 * client proxy before the frame is presented. The compositor delivers the
 * event to a zombie object and destroys its resource; no error, no crash. */
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
    wl_make_toplevel(&top, "presentation-abandoned", 64, 64, 0xffff0000);

    struct wp_presentation_feedback* fb = wp_presentation_feedback(pres, top.surface);
    wl_proxy_destroy((struct wl_proxy*)fb); /* abandoned before any frame */
    wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);

    /* give the frame clock time to present and fire the zombie event */
    for (int i = 0; i < 10; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(30000);
    }
    return 0;
}
