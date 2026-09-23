/* A window points its appmenu at a DBusMenu endpoint, then releases the
 * appmenu object while the window lives on: the window must lose its menu
 * with it, not keep one nobody owns. The scenario asserts the dump at each
 * stage and releases the client through go-files. */

#include "wl_util.h"

#include <appmenu-client-protocol.h>

static struct org_kde_kwin_appmenu_manager* appmenu_manager;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
    (void)d;

    if (!strcmp(iface, org_kde_kwin_appmenu_manager_interface.name)) {
        appmenu_manager = wl_registry_bind(r, name, &org_kde_kwin_appmenu_manager_interface, ver < 2 ? ver : 2);
    }
}

static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}

static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void stage(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);
    printf("stage %s\n", name);

    for (int i = 0; i < 1500; i++) {
        if (access(path, F_OK) == 0) {
            return;
        }

        usleep(20000);
        wl_display_roundtrip(wl_dpy);
    }

    fprintf(stderr, "the scenario never released stage %s\n", name);
    exit(1);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) {
        return 2;
    }

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!appmenu_manager) {
        fprintf(stderr, "no appmenu manager\n");
        return 2;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "appmenu-release", 300, 200, 0xFF405060);

    struct org_kde_kwin_appmenu* appmenu = org_kde_kwin_appmenu_manager_create(appmenu_manager, top.surface);

    org_kde_kwin_appmenu_set_address(appmenu, "org.example.ImwayAppmenuRelease", "/Menu");
    wl_display_roundtrip(wl_dpy);
    stage("attached");

    org_kde_kwin_appmenu_release(appmenu);
    wl_display_roundtrip(wl_dpy);
    stage("released");

    printf("appmenu release done\n");

    return 0;
}
