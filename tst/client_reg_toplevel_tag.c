// xdg-toplevel-tag: the tag a client sets on its toplevel must land in the
// compositor's model (observable through the state dump).

#include "wl_util.h"
#include <xdg-toplevel-tag-v1-client-protocol.h>

static struct xdg_toplevel_tag_manager_v1* tag_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_toplevel_tag_manager_v1_interface.name))
        tag_mgr = wl_registry_bind(r, name, &xdg_toplevel_tag_manager_v1_interface, 1);
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
    if (!tag_mgr) {
        fprintf(stderr, "no xdg_toplevel_tag_manager_v1\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "tag-test", 300, 200, 0xFF208040u);

    xdg_toplevel_tag_manager_v1_set_toplevel_tag(tag_mgr, top.tl, "pip");
    xdg_toplevel_tag_manager_v1_set_toplevel_description(tag_mgr, top.tl,
                                                         "picture in picture");
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_toplevel_tag: tagged\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
