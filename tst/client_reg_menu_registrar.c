/* The registrar side of the appmenu protocol and the removal half of
 * ItemsPropertiesUpdated. The client exports one item carrying every
 * property the compositor understands, then takes each of them away again,
 * asks the registrar to list its menus, asks about a window nobody
 * registered, and finally unregisters. */

#include "wl_util.h"

#include <appmenu-client-protocol.h>

#include <dbus/dbus.h>

static DBusConnection* bus;
static uint32_t layout_revision = 1;
static int layouts_served;
static const char* kMenuPath = "/Menu";
static const char* kRegistrarService = "com.canonical.AppMenu.Registrar";
static const char* kRegistrarPath = "/com/canonical/AppMenu/Registrar";

static void dict_string(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_int(DBusMessageIter* dict, const char* key, int32_t value) {
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_bool(DBusMessageIter* dict, const char* key, dbus_bool_t value) {
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_shortcut(DBusMessageIter* dict, const char* key, const char* mod,
                          const char* letter) {
    DBusMessageIter entry, var, outer, inner;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "aas", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "as", &outer);
    dbus_message_iter_open_container(&outer, DBUS_TYPE_ARRAY, "s", &inner);
    dbus_message_iter_append_basic(&inner, DBUS_TYPE_STRING, &mod);
    dbus_message_iter_append_basic(&inner, DBUS_TYPE_STRING, &letter);
    dbus_message_iter_close_container(&outer, &inner);
    dbus_message_iter_close_container(&var, &outer);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

/* a 1x1 png, enough for the icon-data property to parse */
static const unsigned char kPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
    0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xdd, 0x8d, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
};

static void dict_icon_data(DBusMessageIter* dict) {
    const char* key = "icon-data";
    const unsigned char* bytes = kPng;
    int count = (int)sizeof(kPng);
    DBusMessageIter entry, var, array;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &array);
    dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &bytes, count);
    dbus_message_iter_close_container(&var, &array);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

/* one child holding every property the compositor reads */
static void append_item(DBusMessageIter* children) {
    DBusMessageIter variant, node, props, grandchildren;
    int32_t id = 1;

    dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &node);
    dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &props);
    dict_string(&props, "label", "_Everything");
    dict_bool(&props, "enabled", FALSE);
    dict_bool(&props, "visible", FALSE);
    dict_string(&props, "toggle-type", "checkmark");
    dict_int(&props, "toggle-state", 1);
    dict_string(&props, "children-display", "submenu");
    dict_string(&props, "disposition", "alert");
    dict_shortcut(&props, "shortcut", "Control", "E");
    dict_string(&props, "icon-name", "document-open");
    dict_icon_data(&props);
    dbus_message_iter_close_container(&node, &props);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "v", &grandchildren);
    dbus_message_iter_close_container(&node, &grandchildren);
    dbus_message_iter_close_container(&variant, &node);
    dbus_message_iter_close_container(children, &variant);
}

static void send_layout(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, root, props, children;
    int32_t root_id = 0;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &layout_revision);
    dbus_message_iter_open_container(&it, DBUS_TYPE_STRUCT, NULL, &root);
    dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &root_id);
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}", &props);
    dbus_message_iter_close_container(&root, &props);
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "v", &children);
    append_item(&children);
    dbus_message_iter_close_container(&root, &children);
    dbus_message_iter_close_container(&it, &root);
    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
    layouts_served++;
    printf("layout served %d\n", layouts_served);
}

static void reply_empty(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);

    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c; (void)data;

    if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "GetLayout")) {
        send_layout(msg);
    } else if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "AboutToShow")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_bool_t changed = FALSE;

        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &changed, DBUS_TYPE_INVALID);
        dbus_connection_send(bus, reply, NULL);
        dbus_message_unref(reply);
    } else if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "Event")) {
        reply_empty(msg);
    } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        DBusMessage* err = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "not faked");

        dbus_connection_send(bus, err, NULL);
        dbus_message_unref(err);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_connection_flush(bus);

    return DBUS_HANDLER_RESULT_HANDLED;
}

/* ItemsPropertiesUpdated: nothing updated, everything removed */
static void emit_properties_removed(void) {
    static const char* keys[] = {
        "label", "enabled", "visible", "type", "toggle-type", "toggle-state",
        "children-display", "disposition", "shortcut", "icon-name", "icon-data",
    };
    DBusMessage* sig = dbus_message_new_signal(kMenuPath, "com.canonical.dbusmenu",
                                               "ItemsPropertiesUpdated");
    DBusMessageIter it, updated, removed, row, names;
    int32_t id = 1;

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ia{sv})", &updated);
    dbus_message_iter_close_container(&it, &updated);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ias)", &removed);
    dbus_message_iter_open_container(&removed, DBUS_TYPE_STRUCT, NULL, &row);
    dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&row, DBUS_TYPE_ARRAY, "s", &names);

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        dbus_message_iter_append_basic(&names, DBUS_TYPE_STRING, &keys[i]);
    }

    /* one the compositor does not know, to prove it is ignored */
    const char* unknown = "imway-unknown-property";

    dbus_message_iter_append_basic(&names, DBUS_TYPE_STRING, &unknown);
    dbus_message_iter_close_container(&row, &names);
    dbus_message_iter_close_container(&removed, &row);
    dbus_message_iter_close_container(&it, &removed);
    dbus_connection_send(bus, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(bus);
}

static DBusMessage* registrar_call(const char* method, uint32_t* window) {
    DBusMessage* call = dbus_message_new_method_call(kRegistrarService, kRegistrarPath,
                                                     kRegistrarService, method);

    if (window) {
        dbus_message_append_args(call, DBUS_TYPE_UINT32, window, DBUS_TYPE_INVALID);
    }

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, call, 3000, NULL);

    dbus_message_unref(call);

    return reply;
}

/* the (uso) rows GetMenus returns */
static int menus_contain(DBusMessage* reply, uint32_t window) {
    DBusMessageIter it, array;
    int found = 0;

    if (!reply || !dbus_message_iter_init(reply, &it)) return -1;
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) return -1;
    dbus_message_iter_recurse(&it, &array);

    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
        DBusMessageIter row;
        uint32_t id = 0;
        const char* service = "";
        const char* path = "";

        dbus_message_iter_recurse(&array, &row);
        dbus_message_iter_get_basic(&row, &id);
        dbus_message_iter_next(&row);
        dbus_message_iter_get_basic(&row, &service);
        dbus_message_iter_next(&row);
        dbus_message_iter_get_basic(&row, &path);
        printf("menu row %u %s %s\n", id, service, path);

        if (id == window) found = 1;

        dbus_message_iter_next(&array);
    }

    return found;
}

static struct org_kde_kwin_appmenu_manager* appmenu_manager;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d;
    if (!strcmp(iface, org_kde_kwin_appmenu_manager_interface.name))
        appmenu_manager = wl_registry_bind(r, name, &org_kde_kwin_appmenu_manager_interface,
                                          ver < 2 ? ver : 2);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!bus) {
        fprintf(stderr, "no session bus\n");
        return 2;
    }

    dbus_connection_set_exit_on_disconnect(bus, FALSE);

    if (dbus_bus_request_name(bus, "org.example.ImwayMenuRegistrar", DBUS_NAME_FLAG_DO_NOT_QUEUE,
                              NULL) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "cannot own a bus name\n");
        return 2;
    }

    DBusObjectPathVTable vt = {0};

    vt.message_function = message;
    dbus_connection_register_object_path(bus, kMenuPath, &vt, NULL);

    uint32_t window = 4242;
    DBusMessage* call = dbus_message_new_method_call(kRegistrarService, kRegistrarPath,
                                                     kRegistrarService, "RegisterWindow");

    dbus_message_append_args(call, DBUS_TYPE_UINT32, &window, DBUS_TYPE_OBJECT_PATH, &kMenuPath,
                             DBUS_TYPE_INVALID);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, call, 3000, NULL);

    dbus_message_unref(call);

    if (!reply) {
        fprintf(stderr, "RegisterWindow got no reply\n");
        return 1;
    }

    dbus_message_unref(reply);
    puts("window registered");

    /* A registration alone is bookkeeping; the compositor pulls a menu for a
     * window it can see. Map one and point it at this service. */
    if (wl_boot()) {
        fprintf(stderr, "no wayland display\n");
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

    wl_make_toplevel(&top, "menu-registrar", 400, 300, 0xFF505060);

    struct org_kde_kwin_appmenu* appmenu =
        org_kde_kwin_appmenu_manager_create(appmenu_manager, top.surface);

    org_kde_kwin_appmenu_set_address(appmenu, "org.example.ImwayMenuRegistrar", kMenuPath);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    puts("window mapped");

    /* the compositor pulls the layout in; serve it */
    for (int i = 0; i < 200 && !layouts_served; i++) {
        wl_display_roundtrip(wl_dpy);
        dbus_connection_read_write_dispatch(bus, 50);
    }

    if (!layouts_served) {
        fprintf(stderr, "the compositor never asked for the layout\n");
        return 1;
    }

    reply = registrar_call("GetMenus", NULL);

    int found = menus_contain(reply, window);

    if (reply) dbus_message_unref(reply);

    if (found != 1) {
        fprintf(stderr, "GetMenus did not list the window (%d)\n", found);
        return 1;
    }

    puts("menus listed");

    uint32_t stranger = 999999;

    reply = registrar_call("GetMenuForWindow", &stranger);

    if (reply) {
        int is_error = dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR;

        dbus_message_unref(reply);

        if (!is_error) {
            fprintf(stderr, "an unregistered window was answered\n");
            return 1;
        }
    }

    puts("stranger refused");

    emit_properties_removed();

    for (int i = 0; i < 40; i++) {
        wl_display_roundtrip(wl_dpy);
        dbus_connection_read_write_dispatch(bus, 50);
    }

    puts("properties removed");

    reply = registrar_call("UnregisterWindow", &window);

    if (reply) dbus_message_unref(reply);

    for (int i = 0; i < 20; i++) {
        wl_display_roundtrip(wl_dpy);
        dbus_connection_read_write_dispatch(bus, 50);
    }

    reply = registrar_call("GetMenus", NULL);
    found = menus_contain(reply, window);

    if (reply) dbus_message_unref(reply);

    if (found != 0) {
        fprintf(stderr, "the window is still registered (%d)\n", found);
        return 1;
    }

    puts("menu registrar done");

    return 0;
}
