#include "wl_util.h"

#include <security-context-v1-client-protocol.h>
#include <ext-data-control-v1-client-protocol.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>

// #S-16: security-context. A sandbox proxies clients through a listen socket;
// the compositor tags them and hides privileged globals. This client plays
// the sandbox: it hands the compositor a listening socket, then connects a
// second display through it and asserts the sandboxed client sees
// wl_compositor but NOT ext_data_control_manager_v1.

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

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!sec_mgr) {
        fprintf(stderr, "no wp_security_context_manager_v1\n");
        return 2;
    }

    // a listening AF_UNIX socket the compositor will accept sandboxed clients on
    const char* rundir = getenv("XDG_RUNTIME_DIR");
    char path[256];
    snprintf(path, sizeof(path), "%s/sandbox-sock", rundir ? rundir : "/tmp");
    unlink(path);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(listen_fd, 4) < 0) {
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

    // now connect a sandboxed client through the proxied socket
    struct wl_display* sb = wl_display_connect(path);
    if (!sb) {
        fprintf(stderr, "sandboxed connect failed\n");
        return 1;
    }
    struct wl_registry* sb_reg = wl_display_get_registry(sb);
    wl_registry_add_listener(sb_reg, &sb_listener, NULL);
    wl_display_roundtrip(sb);
    wl_display_roundtrip(sb);

    if (!saw_compositor) {
        fprintf(stderr, "sandboxed client did not see wl_compositor\n");
        return 1;
    }
    if (saw_data_control) {
        fprintf(stderr, "sandboxed client saw the privileged ext_data_control\n");
        return 1;
    }

    wl_display_disconnect(sb);
    printf("security-context done\n");
    fflush(stdout);
    return 0;
}
