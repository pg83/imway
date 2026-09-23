// xdg-foreign-v2: an exported toplevel's handle imported by another party
// must attach the importer's toplevel as a child of the exported one, and
// revoking the export must break the relationship with a destroyed event,
// leaving the import of another window's export alone.

#include "wl_util.h"
#include <xdg-foreign-unstable-v2-client-protocol.h>

static struct zxdg_exporter_v2* exporter;
static struct zxdg_importer_v2* importer;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zxdg_exporter_v2_interface.name))
        exporter = wl_registry_bind(r, name, &zxdg_exporter_v2_interface, 1);
    else if (!strcmp(iface, zxdg_importer_v2_interface.name))
        importer = wl_registry_bind(r, name, &zxdg_importer_v2_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static char handle[256];
static int imported_destroyed;

static void exported_handle(void* d, struct zxdg_exported_v2* e, const char* h) {
    (void)d; (void)e;
    snprintf(handle, sizeof(handle), "%s", h);
}
static const struct zxdg_exported_v2_listener exported_listener = {exported_handle};

static void imported_gone(void* d, struct zxdg_imported_v2* i) {
    (void)d; (void)i;
    imported_destroyed = 1;
}
static const struct zxdg_imported_v2_listener imported_listener = {imported_gone};

// the child's own export, imported too
static char other_handle[256];
static int other_destroyed;

static void other_exported_handle(void* d, struct zxdg_exported_v2* e, const char* h) {
    (void)d; (void)e;
    snprintf(other_handle, sizeof(other_handle), "%s", h);
}
static const struct zxdg_exported_v2_listener other_exported_listener = {other_exported_handle};

static void other_imported_gone(void* d, struct zxdg_imported_v2* i) {
    (void)d; (void)i;
    other_destroyed = 1;
}
static const struct zxdg_imported_v2_listener other_imported_listener = {other_imported_gone};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!exporter || !importer) {
        fprintf(stderr, "no zxdg_exporter_v2/zxdg_importer_v2\n");
        return 1;
    }

    struct wl_toplevel_ctx parent;
    wl_make_toplevel(&parent, "foreign-parent", 400, 300, 0xFF804020u);

    struct zxdg_exported_v2* exported =
        zxdg_exporter_v2_export_toplevel(exporter, parent.surface);
    zxdg_exported_v2_add_listener(exported, &exported_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!handle[0]) {
        fprintf(stderr, "no handle from the exporter\n");
        return 1;
    }

    struct wl_toplevel_ctx child;
    wl_make_toplevel(&child, "foreign-child", 200, 150, 0xFF204080u);

    struct zxdg_imported_v2* imported =
        zxdg_importer_v2_import_toplevel(importer, handle);
    zxdg_imported_v2_add_listener(imported, &imported_listener, NULL);
    zxdg_imported_v2_set_parent_of(imported, child.surface);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_xdg_foreign: attached\n");

    struct zxdg_exported_v2* other_exported = zxdg_exporter_v2_export_toplevel(exporter, child.surface);

    zxdg_exported_v2_add_listener(other_exported, &other_exported_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!other_handle[0]) {
        fprintf(stderr, "no handle for the second export\n");
        return 1;
    }

    struct zxdg_imported_v2* other_imported = zxdg_importer_v2_import_toplevel(importer, other_handle);

    zxdg_imported_v2_add_listener(other_imported, &other_imported_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (other_destroyed) {
        fprintf(stderr, "the second window's handle did not import\n");
        return 1;
    }

    // scenario checkpoint, then revoke the export
    wlk_watch_key = 57; // KEY_SPACE
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    zxdg_exported_v2_destroy(exported);
    wl_display_roundtrip(wl_dpy);
    if (!imported_destroyed) {
        fprintf(stderr, "no destroyed event after the export was revoked\n");
        return 1;
    }
    if (other_destroyed) {
        fprintf(stderr, "revoking one export destroyed the import of another\n");
        return 1;
    }
    printf("client_reg_xdg_foreign: revoked\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
