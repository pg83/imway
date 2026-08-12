// ext-foreign-toplevel-list: an app_id change after the window is mapped must
// reach a listing client, exactly like a title change does.

#include "wl_util.h"
#include <ext-foreign-toplevel-list-v1-client-protocol.h>

static struct ext_foreign_toplevel_list_v1* list;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, ext_foreign_toplevel_list_v1_interface.name))
        list = wl_registry_bind(r, name, &ext_foreign_toplevel_list_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static char last_app_id[128];
static int app_id_events;

static void h_closed(void* d, struct ext_foreign_toplevel_handle_v1* h) { (void)d;(void)h; }
static void h_done(void* d, struct ext_foreign_toplevel_handle_v1* h) { (void)d;(void)h; }
static void h_title(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* t) { (void)d;(void)h;(void)t; }
static void h_app_id(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* a) {
    (void)d; (void)h;
    snprintf(last_app_id, sizeof(last_app_id), "%s", a);
    app_id_events++;
}
static void h_identifier(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* i) { (void)d;(void)h;(void)i; }
static const struct ext_foreign_toplevel_handle_v1_listener handle_listener = {
    .closed = h_closed,
    .done = h_done,
    .title = h_title,
    .app_id = h_app_id,
    .identifier = h_identifier,
};

static void l_toplevel(void* d, struct ext_foreign_toplevel_list_v1* l,
                       struct ext_foreign_toplevel_handle_v1* h) {
    (void)d; (void)l;
    ext_foreign_toplevel_handle_v1_add_listener(h, &handle_listener, NULL);
}
static void l_finished(void* d, struct ext_foreign_toplevel_list_v1* l) { (void)d; (void)l; }
static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
    .toplevel = l_toplevel,
    .finished = l_finished,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!list) {
        fprintf(stderr, "no ext_foreign_toplevel_list_v1\n");
        return 1;
    }

    ext_foreign_toplevel_list_v1_add_listener(list, &list_listener, NULL);

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "appid-initial", 300, 200, 0xFF3060A0u);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    // the initial announcement carried app_id == title
    if (strcmp(last_app_id, "appid-initial") != 0) {
        fprintf(stderr, "initial app_id not announced: '%s'\n", last_app_id);
        return 1;
    }

    // change app_id after the map; the listing client must be told
    xdg_toplevel_set_app_id(top.tl, "appid-changed");
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (strcmp(last_app_id, "appid-changed") != 0) {
        fprintf(stderr, "app_id change not propagated: still '%s'\n", last_app_id);
        return 1;
    }

    printf("client_reg_foreign_toplevel_appid: app_id updated\n");
    return 0;
}
