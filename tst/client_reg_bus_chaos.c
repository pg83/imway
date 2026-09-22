/* The session-bus services when libdbus runs dry, driven by the chaos
 * words in the scenario header. A window's menu at this connection's
 * unique name: its layout call is first not sent at all, then sent without
 * a reply notify (the answer is lost), then not even built, and each
 * LayoutUpdated tries again until one gets through. A registration whose WindowRegistered signal cannot be
 * built still registers. A menu at a well-known name whose owner lookup
 * cannot be built ignores the owner's signals until the name changes
 * hands. A tray item's property read fails the same three ways and each
 * NewIcon retries it. The scenario checks the compositor's view in the
 * dump between the stages, handed over through go-files. */

#include "wl_util.h"

#include <appmenu-client-protocol.h>

#include <dbus/dbus.h>

#include <stdarg.h>

static DBusConnection* bus;
static const char* kName = "org.example.ImwayBusChaos";
static const char* kRegistrar = "com.canonical.AppMenu.Registrar";
static const char* kRegistrarPath = "/com/canonical/AppMenu/Registrar";
static int layouts;
static int getalls;
static int registered_signals;
static int unregistered_signals;
static struct org_kde_kwin_appmenu_manager* appmenu_manager;

static void pump(int ms) {
    for (int i = 0; i < ms / 10; i++) {
        wl_display_flush(wl_dpy);
        wl_display_dispatch_pending(wl_dpy);
        dbus_connection_read_write_dispatch(bus, 10);
    }
}

static void stage(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);
    printf("stage %s\n", name);

    for (int i = 0; i < 3000; i++) {
        if (access(path, F_OK) == 0) {
            return;
        }

        pump(20);
    }

    fprintf(stderr, "the scenario never released stage %s\n", name);
    exit(1);
}

static void send_layout(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, root, props, children, var, node, kprops, kids, entry, v;
    uint32_t revision = 1;
    int32_t root_id = 0, id = 1;
    const char* key = "label";
    const char* label = "Chaos";

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
}

static void send_properties(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, dict, entry, var;
    const char* key = "Id";
    const char* value = "chaos-item";

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)data;

    if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "GetLayout")) {
        layouts++;
        printf("getlayout %d\n", layouts);
        send_layout(msg);
    } else if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        getalls++;
        printf("getall %d\n", getalls);
        send_properties(msg);
    } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        DBusMessage* err = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "not faked");

        dbus_connection_send(bus, err, NULL);
        dbus_message_unref(err);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_connection_flush(c);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult filter(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c;
    (void)data;

    if (dbus_message_is_signal(msg, kRegistrar, "WindowRegistered")) {
        registered_signals++;
    } else if (dbus_message_is_signal(msg, kRegistrar, "WindowUnregistered")) {
        unregistered_signals++;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void emit(DBusMessage* sig) {
    dbus_connection_send(bus, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(bus);
}

/* revision 1 is the one served: once the model holds it, this refreshes
 * nothing more */
static void layout_updated(const char* path) {
    DBusMessage* sig = dbus_message_new_signal(path, "com.canonical.dbusmenu", "LayoutUpdated");
    uint32_t revision = 1;
    int32_t parent = 0;

    dbus_message_append_args(sig, DBUS_TYPE_UINT32, &revision, DBUS_TYPE_INT32, &parent, DBUS_TYPE_INVALID);
    emit(sig);
}

static void activation(const char* path, int32_t id) {
    DBusMessage* sig = dbus_message_new_signal(path, "com.canonical.dbusmenu", "ItemActivationRequested");
    uint32_t stamp = 0;

    dbus_message_append_args(sig, DBUS_TYPE_INT32, &id, DBUS_TYPE_UINT32, &stamp, DBUS_TYPE_INVALID);
    emit(sig);
}

/* retry with a signal until the counter moves; returns how many it took */
static int retry_until(int* counter, int want, void (*poke)(void)) {
    for (int tries = 1; tries <= 10; tries++) {
        poke();

        for (int i = 0; i < 150 && *counter < want; i++) {
            pump(10);
        }

        if (*counter >= want) {
            return tries;
        }
    }

    fprintf(stderr, "no call got through after ten tries\n");
    exit(1);
}

static void poke_layout(void) {
    layout_updated("/Menu");
}

static void poke_icon(void) {
    emit(dbus_message_new_signal("/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewIcon"));
}

static DBusMessage* call_blocking(const char* dest, const char* path, const char* iface, const char* method,
                                  int first, ...) {
    DBusMessage* call = dbus_message_new_method_call(dest, path, iface, method);
    va_list args;

    va_start(args, first);
    dbus_message_append_args_valist(call, first, args);
    va_end(args);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, call, 3000, NULL);

    dbus_message_unref(call);

    if (!reply) {
        fprintf(stderr, "%s got no answer\n", method);
        exit(1);
    }

    return reply;
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
    alarm(90);

    bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!bus) {
        return 2;
    }

    dbus_connection_set_exit_on_disconnect(bus, FALSE);

    DBusObjectPathVTable vt = {0};

    vt.message_function = message;
    dbus_connection_register_fallback(bus, "/", &vt, NULL);
    dbus_connection_add_filter(bus, filter, NULL, NULL);
    dbus_bus_add_match(bus, "type='signal',interface='com.canonical.AppMenu.Registrar'", NULL);

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

    wl_make_toplevel(&top, "bus-chaos", 300, 200, 0xFF306040);

    struct org_kde_kwin_appmenu* appmenu = org_kde_kwin_appmenu_manager_create(appmenu_manager, top.surface);

    /* at the unique name: no owner lookup, straight to the layout */
    org_kde_kwin_appmenu_set_address(appmenu, dbus_bus_get_unique_name(bus), "/Menu");
    wl_display_roundtrip(wl_dpy);
    pump(300);

    if (layouts) {
        fprintf(stderr, "the first layout call got through\n");
        return 1;
    }

    /* the lost answer leaves the model unready, so the update after it
     * still refreshes */
    printf("2 layouts after %d updates\n", retry_until(&layouts, 2, poke_layout));
    stage("layout");

    /* a registration without its signal, an unregistration with it */
    uint32_t window = 77;
    const char* menu_path = "/Menu";
    DBusMessage* reply;

    reply = call_blocking(kRegistrar, kRegistrarPath, kRegistrar, "RegisterWindow", DBUS_TYPE_UINT32, &window,
                          DBUS_TYPE_OBJECT_PATH, &menu_path, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    reply = call_blocking(kRegistrar, kRegistrarPath, kRegistrar, "UnregisterWindow", DBUS_TYPE_UINT32, &window,
                          DBUS_TYPE_INVALID);
    dbus_message_unref(reply);

    for (int i = 0; i < 100 && !unregistered_signals; i++) {
        pump(10);
    }

    if (registered_signals || unregistered_signals != 1) {
        fprintf(stderr, "registrar signals: %d registered, %d unregistered\n", registered_signals,
                unregistered_signals);
        return 1;
    }

    puts("registered without its signal");

    /* at a well-known name whose owner lookup fails: the owner's signals
     * do not count until the name changes hands */
    if (dbus_bus_request_name(bus, kName, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        return 2;
    }

    org_kde_kwin_appmenu_set_address(appmenu, kName, "/Menu");
    wl_display_roundtrip(wl_dpy);

    for (int i = 0; i < 300 && layouts < 3; i++) {
        pump(10);
    }

    activation("/Menu", 9);
    pump(200);
    stage("ownerless");

    dbus_bus_release_name(bus, kName, NULL);
    pump(200);
    dbus_bus_request_name(bus, kName, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL);

    for (int i = 0; i < 300 && layouts < 4; i++) {
        pump(10);
    }

    activation("/Menu", 9);
    stage("owned");

    /* the tray item's property read fails three ways */
    const char* item_path = "/StatusNotifierItem";

    reply = call_blocking("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                          "RegisterStatusNotifierItem", DBUS_TYPE_STRING, &item_path, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    pump(300);

    if (getalls) {
        fprintf(stderr, "the first property read got through\n");
        return 1;
    }

    printf("2 reads after %d icons\n", retry_until(&getalls, 2, poke_icon));
    stage("tray");
    puts("bus chaos done");

    return 0;
}
