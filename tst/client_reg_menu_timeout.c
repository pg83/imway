/* A window menu whose first GetLayout is never answered. The compositor's
 * call times out after its three seconds (a libdbus timeout on the
 * compositor's loop); the menu stays empty meanwhile, and the next
 * LayoutUpdated is served and fills it. The held call is answered late,
 * after the timeout, with a label that must never show. */
#include "wl_util.h"

#include <appmenu-client-protocol.h>

#include <dbus/dbus.h>

static DBusConnection* bus;
static DBusMessage* held;
static int layouts;
static struct org_kde_kwin_appmenu_manager* appmenu_manager;

static void pump(int ms) {
    for (int i = 0; i < ms / 10; i++) {
        wl_display_flush(wl_dpy);
        wl_display_dispatch_pending(wl_dpy);
        dbus_connection_read_write_dispatch(bus, 10);
    }
}

static void send_layout(DBusMessage* call, const char* label) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, root, props, children, var, node, kprops, kids, entry, v;
    uint32_t revision = 1;
    int32_t root_id = 0, id = 1;
    const char* key = "label";

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &revision);
    dbus_message_iter_open_container(&it, DBUS_TYPE_STRUCT, NULL, &root);
    dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &root_id);
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}", &props);
    dbus_message_iter_close_container(&root, &props);
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "v", &children);
    dbus_message_iter_open_container(&children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_STRUCT, NULL, &node);
    dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &kprops);
    dbus_message_iter_open_container(&kprops, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &label);
    dbus_message_iter_close_container(&entry, &v);
    dbus_message_iter_close_container(&kprops, &entry);
    dbus_message_iter_close_container(&node, &kprops);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "v", &kids);
    dbus_message_iter_close_container(&node, &kids);
    dbus_message_iter_close_container(&var, &node);
    dbus_message_iter_close_container(&children, &var);
    dbus_message_iter_close_container(&root, &children);
    dbus_message_iter_close_container(&it, &root);
    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
    dbus_connection_flush(bus);
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c;
    (void)data;

    if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "GetLayout")) {
        layouts++;
        printf("getlayout %d\n", layouts);

        if (layouts == 1) {
            held = dbus_message_ref(msg);
        } else {
            send_layout(msg, "Fresh");
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

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

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!bus) {
        return 2;
    }

    DBusObjectPathVTable vt = {0};

    vt.message_function = message;
    dbus_connection_register_fallback(bus, "/", &vt, NULL);

    if (wl_boot()) {
        return 2;
    }

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!appmenu_manager) {
        return 2;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "menu-timeout", 300, 200, 0xFF304060);

    struct org_kde_kwin_appmenu* appmenu = org_kde_kwin_appmenu_manager_create(appmenu_manager, top.surface);

    org_kde_kwin_appmenu_set_address(appmenu, dbus_bus_get_unique_name(bus), "/Menu");
    wl_display_roundtrip(wl_dpy);

    for (int i = 0; i < 500 && !layouts; i++) {
        pump(10);
    }

    if (!held) {
        fprintf(stderr, "the layout was never asked for\n");
        return 1;
    }

    puts("layout held");

    /* past the compositor's three second timeout, then the late answer */
    pump(4500);
    send_layout(held, "Stale");
    dbus_message_unref(held);
    puts("timeout passed");

    DBusMessage* sig = dbus_message_new_signal("/Menu", "com.canonical.dbusmenu", "LayoutUpdated");
    uint32_t revision = 0;
    int32_t parent = 0;

    dbus_message_append_args(sig, DBUS_TYPE_UINT32, &revision, DBUS_TYPE_INT32, &parent, DBUS_TYPE_INVALID);
    dbus_connection_send(bus, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(bus);

    for (int i = 0; i < 500 && layouts < 2; i++) {
        pump(10);
    }

    puts(layouts >= 2 ? "layout served" : "layout never asked again");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
