#include "wl_util.h"

#include <security-context-v1-client-protocol.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

// security-context: the sandbox going away (its close fd hung up) while the
// context object is still alive. The compositor must stop listening at once
// — the socket, whose last copy it holds, then refuses connections — and the
// context destroyed afterwards must go without touching what was already
// torn down.

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

static struct sockaddr_un addr;
static socklen_t addr_len;

// 1 if the sandbox socket accepted a connection, 0 if it was refused
static int try_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int rc = connect(fd, (struct sockaddr*)&addr, addr_len);
    int refused = rc < 0 && errno == ECONNREFUSED;

    close(fd);

    if (rc < 0 && !refused) {
        perror("connect");
        exit(2);
    }

    return !refused;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!sec_mgr) {
        fprintf(stderr, "no wp_security_context_manager_v1\n");
        return 2;
    }

    // an abstract name: once the compositor closes the last copy, nothing
    // answers on it
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "imway-sandbox-close-%d", getpid());
    addr_len = (socklen_t)(sizeof(addr.sun_family) + 1 + strlen(addr.sun_path + 1));

    int listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int hangup[2];

    if (listen_fd < 0 || bind(listen_fd, (struct sockaddr*)&addr, addr_len) < 0 ||
        listen(listen_fd, 4) < 0 || pipe(hangup) < 0) {
        perror("sandbox socket");
        return 2;
    }

    struct wp_security_context_v1* ctx =
        wp_security_context_manager_v1_create_listener(sec_mgr, listen_fd, hangup[0]);

    wp_security_context_v1_set_sandbox_engine(ctx, "imway.test");
    wp_security_context_v1_set_app_id(ctx, "imway.test.close");
    wp_security_context_v1_set_instance_id(ctx, "1");
    wp_security_context_v1_commit(ctx);
    wl_display_roundtrip(wl_dpy);
    close(listen_fd);
    close(hangup[0]);

    if (!try_connect()) {
        fprintf(stderr, "the committed context does not listen\n");
        return 1;
    }

    // the sandbox is gone
    close(hangup[1]);

    int refused = 0;

    for (int i = 0; i < 100 && !refused; i++) {
        wl_display_roundtrip(wl_dpy);
        refused = !try_connect();
        if (!refused) {
            struct timespec ts = {0, 20 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
    }

    if (!refused) {
        fprintf(stderr, "the socket still listens after the sandbox hung up\n");
        return 1;
    }

    printf("listening stopped\n");

    wp_security_context_v1_destroy(ctx);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "destroying the stopped context failed\n");
        return 1;
    }

    printf("security close done\n");

    return 0;
}
