// xdg-dialog: the compositor must advertise the global and record modality.

#include "wl_util.h"
#include <xdg-dialog-v1-client-protocol.h>

static struct xdg_wm_dialog_v1* dialog_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_wm_dialog_v1_interface.name))
        dialog_mgr = wl_registry_bind(r, name, &xdg_wm_dialog_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dialog_mgr) {
        fprintf(stderr, "no xdg_wm_dialog\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "xdg-dialog", 200, 150, 0xFFA05030u);

    struct xdg_dialog_v1* dlg = xdg_wm_dialog_v1_get_xdg_dialog(dialog_mgr, top.tl);
    xdg_dialog_v1_set_modal(dlg);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_xdg_dialog: modal\n");

    // wait for a key so the scenario can read the modal state, then unset
    wlk_watch_key = 57;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    xdg_dialog_v1_unset_modal(dlg);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_xdg_dialog: modeless\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
