#include "wl_util.h"

#include <security-context-v1-client-protocol.h>
#include <ext-data-control-v1-client-protocol.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>

// security-context under IMWAY_CHAOS=security-accept=1: the compositor's
// accept of the first sandboxed connection fails (as with EMFILE or an
// aborted connection). That client is dropped; the listener keeps working
// and the next sandboxed client connects and sees the core globals.

static struct wp_security_context_manager_v1* sec_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, wp_security_context_manager_v1_interface.name))
        sec_mgr = wl_registry_bind(r, name, &wp_security_context_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int saw_compositor, saw_data_control;
static void sb_global(void* d, struct wl_registry* r, uint32_t name,
                      const char* iface, uint32_t ver) {
    (void)d; (void)r; (void)name; (void)ver;
    if (!strcmp(iface, wl_compositor_interface.name)) saw_compositor = 1;
    else if (!strcmp(iface, ext_data_control_manager_v1_interface.name)) saw_data_control = 1;
}
static const struct wl_registry_listener sb_listener = {sb_global, extra_remove};

static int sandboxed_roundtrip(const char* path) {
    struct wl_display* sb = wl_display_connect(path);

    if (!sb) return -1;

    struct wl_registry* reg = wl_display_get_registry(sb);

    wl_registry_add_listener(reg, &sb_listener, NULL);

    int rc = wl_display_roundtrip(sb);

    wl_registry_destroy(reg);
    wl_display_disconnect(sb);
    return rc;
}

int main(void) {
    alarm(20);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!sec_mgr) {
        fprintf(stderr, "no wp_security_context_manager_v1\n");
        return 2;
    }

    const char* rundir = getenv("XDG_RUNTIME_DIR");
    char path[256];
    snprintf(path, sizeof(path), "%s/sb-accept", rundir ? rundir : "/tmp");
    unlink(path);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(listen_fd, 4) < 0) {
        perror("listen socket");
        return 2;
    }
    int close_fd = eventfd(0, EFD_CLOEXEC);

    struct wp_security_context_v1* ctx =
        wp_security_context_manager_v1_create_listener(sec_mgr, listen_fd, close_fd);
    wp_security_context_v1_set_sandbox_engine(ctx, "org.flatpak");
    wp_security_context_v1_set_app_id(ctx, "org.example.Sandboxed");
    wp_security_context_v1_set_instance_id(ctx, "instance-1");
    wp_security_context_v1_commit(ctx);
    wl_display_roundtrip(wl_dpy);

    if (sandboxed_roundtrip(path) >= 0) {
        fprintf(stderr, "the connection whose accept failed was served\n");
        return 1;
    }
    saw_compositor = 0;
    if (sandboxed_roundtrip(path) < 0 || !saw_compositor) {
        fprintf(stderr, "the listener did not serve the next sandboxed client\n");
        return 1;
    }
    printf("security accept fault done\n");
    return 0;
}
