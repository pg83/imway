#define _GNU_SOURCE

#include <appmenu-client-protocol.h>
#include <xdg-shell-client-protocol.h>

#include <dbus/dbus.h>
#include <wayland-client.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static struct wl_display* display;
static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wm_base;
static struct org_kde_kwin_appmenu_manager* appmenu_manager;
static DBusConnection* bus;
static bool configured;
static bool mapped_printed;
static bool lazy_ready;
static bool got_event;
static bool failed;
static uint32_t layout_revision = 1;

static void dict_string(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_bool(DBusMessageIter* dict, const char* key, dbus_bool_t value) {
    DBusMessageIter entry, variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_int(DBusMessageIter* dict, const char* key, int32_t value) {
    DBusMessageIter entry, variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_shortcut(DBusMessageIter* dict, const char* key_name, const char* modifier, const char* key_name_value) {
    DBusMessageIter entry, variant, alternatives, chord;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key_name);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "aas", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "as", &alternatives);
    dbus_message_iter_open_container(&alternatives, DBUS_TYPE_ARRAY, "s", &chord);
    dbus_message_iter_append_basic(&chord, DBUS_TYPE_STRING, &modifier);
    dbus_message_iter_append_basic(&chord, DBUS_TYPE_STRING, &key_name_value);
    dbus_message_iter_close_container(&alternatives, &chord);
    dbus_message_iter_close_container(&variant, &alternatives);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_icon_data(DBusMessageIter* dict) {
    // Valid 1x1 RGBA PNG, transported as the DBusMenu icon-data byte array.
    static const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
        0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99,
        0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    const char* key = "icon-data";
    const unsigned char* bytes = png;
    int count = (int)sizeof(png);
    DBusMessageIter entry, variant, array;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "y", &array);
    dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &bytes, count);
    dbus_message_iter_close_container(&variant, &array);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_empty_children(DBusMessageIter* node) {
    DBusMessageIter children;

    dbus_message_iter_open_container(node, DBUS_TYPE_ARRAY, "v", &children);
    dbus_message_iter_close_container(node, &children);
}

static void append_leaf(DBusMessageIter* parent, int32_t id, const char* label, int kind) {
    DBusMessageIter node, props;

    dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, NULL, &node);
    dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &props);

    if (label) {
        dict_string(&props, "label", label);
    }

    if (kind == 1) {
        dict_string(&props, "type", "separator");
    } else if (kind == 2) {
        dict_string(&props, "toggle-type", "checkmark");
        dict_int(&props, "toggle-state", 0);
    } else if (kind == 3) {
        dict_string(&props, "toggle-type", "radio");
        dict_int(&props, "toggle-state", 1);
    } else if (kind == 4) {
        dict_bool(&props, "enabled", FALSE);
    } else if (kind == 5) {
        dict_bool(&props, "visible", FALSE);
    } else if (kind == 6) {
        dict_string(&props, "disposition", "alert");
        dict_shortcut(&props, "shortcut", "Control", "Q");
        dict_icon_data(&props);
    }

    dbus_message_iter_close_container(&node, &props);
    append_empty_children(&node);
    dbus_message_iter_close_container(parent, &node);
}

static void append_variant_leaf(DBusMessageIter* children, int32_t id, const char* label, int kind) {
    DBusMessageIter variant;

    dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &variant);
    append_leaf(&variant, id, label, kind);
    dbus_message_iter_close_container(children, &variant);
}

static void append_recent(DBusMessageIter* children) {
    DBusMessageIter variant, node, props, grandchildren;
    int32_t id = 11;

    dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &node);
    dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &props);
    dict_string(&props, "label", "_Recent");
    dict_string(&props, "children-display", "submenu");
    dict_string(&props, "icon-name", "document-open-recent");
    dbus_message_iter_close_container(&node, &props);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "v", &grandchildren);

    if (lazy_ready) {
        append_variant_leaf(&grandchildren, 110, "Lazy document", 0);
    }

    dbus_message_iter_close_container(&node, &grandchildren);
    dbus_message_iter_close_container(&variant, &node);
    dbus_message_iter_close_container(children, &variant);
}

static void append_file_menu(DBusMessageIter* root_children) {
    DBusMessageIter variant, node, props, children;
    int32_t id = 1;

    dbus_message_iter_open_container(root_children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &node);
    dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &props);
    dict_string(&props, "label", "_File");
    dict_string(&props, "children-display", "submenu");
    dbus_message_iter_close_container(&node, &props);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "v", &children);
    append_variant_leaf(&children, 10, "_Open conform item", 0);
    append_variant_leaf(&children, 12, NULL, 1);
    append_recent(&children);
    append_variant_leaf(&children, 20, "_Checked option", 2);
    append_variant_leaf(&children, 21, "_Radio option", 3);
    append_variant_leaf(&children, 22, "Disabled option", 4);
    append_variant_leaf(&children, 23, "Invisible option", 5);
    append_variant_leaf(&children, 25, "_Quit danger", 6);
    dbus_message_iter_close_container(&node, &children);
    dbus_message_iter_close_container(&variant, &node);
    dbus_message_iter_close_container(root_children, &variant);
}

static void send_layout(DBusMessage* call) {
    DBusMessageIter args;
    int32_t parent = -99, depth = 0;

    if (!dbus_message_iter_init(call, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_INT32) {
        failed = true;
    } else {
        dbus_message_iter_get_basic(&args, &parent);
        dbus_message_iter_next(&args);
        dbus_message_iter_get_basic(&args, &depth);
    }

    if (parent != 0 || depth != -1) {
        fprintf(stderr, "non-conform GetLayout(%d,%d)\n", parent, depth);
        failed = true;
    }

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
    append_file_menu(&children);
    append_variant_leaf(&children, 2, "_Help", 0);
    dbus_message_iter_close_container(&root, &children);
    dbus_message_iter_close_container(&it, &root);
    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
    printf("layout revision %u\n", layout_revision);
}

static void emit_layout_updated(void) {
    DBusMessage* signal = dbus_message_new_signal("/Menu", "com.canonical.dbusmenu", "LayoutUpdated");
    int32_t parent = 0;

    dbus_message_append_args(signal, DBUS_TYPE_UINT32, &layout_revision,
                             DBUS_TYPE_INT32, &parent, DBUS_TYPE_INVALID);
    dbus_connection_send(bus, signal, NULL);
    dbus_message_unref(signal);
}

static void emit_properties_updated(void) {
    DBusMessage* signal = dbus_message_new_signal("/Menu", "com.canonical.dbusmenu", "ItemsPropertiesUpdated");
    DBusMessageIter it, updated, row, props, removed;
    int32_t id = 20;

    dbus_message_iter_init_append(signal, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ia{sv})", &updated);
    dbus_message_iter_open_container(&updated, DBUS_TYPE_STRUCT, NULL, &row);
    dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&row, DBUS_TYPE_ARRAY, "{sv}", &props);
    dict_int(&props, "toggle-state", 1);
    dbus_message_iter_close_container(&row, &props);
    dbus_message_iter_close_container(&updated, &row);
    dbus_message_iter_close_container(&it, &updated);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ias)", &removed);
    dbus_message_iter_close_container(&it, &removed);
    dbus_connection_send(bus, signal, NULL);
    dbus_message_unref(signal);

    signal = dbus_message_new_signal("/Menu", "com.canonical.dbusmenu", "ItemActivationRequested");
    uint32_t stamp = 1;

    dbus_message_append_args(signal, DBUS_TYPE_INT32, &id,
                             DBUS_TYPE_UINT32, &stamp, DBUS_TYPE_INVALID);
    dbus_connection_send(bus, signal, NULL);
    dbus_message_unref(signal);
    puts("property and activation signals sent");
}

static DBusHandlerResult menu_message(DBusConnection* connection, DBusMessage* msg, void* data) {
    (void)connection;
    (void)data;

    if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "GetLayout")) {
        send_layout(msg);
    } else if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "AboutToShow")) {
        int32_t id = -1;

        dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &id, DBUS_TYPE_INVALID);
        printf("about %d\n", id);

        dbus_bool_t update = FALSE;

        if (id == 11 && !lazy_ready) {
            lazy_ready = true;
            layout_revision++;
            update = TRUE;
        }

        DBusMessage* reply = dbus_message_new_method_return(msg);

        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &update, DBUS_TYPE_INVALID);
        dbus_connection_send(bus, reply, NULL);
        dbus_message_unref(reply);

        if (update) {
            emit_layout_updated();
        }
    } else if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "Event")) {
        int32_t id = -1;

        // Parse only the stable first argument: the payload is deliberately
        // allowed to be any variant by the protocol.
        DBusMessageIter it;

        if (dbus_message_iter_init(msg, &it)) {
            dbus_message_iter_get_basic(&it, &id);
        }

        DBusMessage* reply = dbus_message_new_method_return(msg);

        dbus_connection_send(bus, reply, NULL);
        dbus_message_unref(reply);
        printf("event %d\n", id);
        got_event = id == 10;
        emit_properties_updated();
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_connection_flush(bus);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusMessage* blocking_call(DBusMessage* call) {
    DBusError error;

    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, call, 3000, &error);

    dbus_message_unref(call);

    if (!reply) {
        fprintf(stderr, "DBus call failed: %s\n", error.message ? error.message : "unknown");
        failed = true;
    }

    dbus_error_free(&error);

    return reply;
}

static void test_registrar(void) {
    DBusMessage* call = dbus_message_new_method_call(
        "com.canonical.AppMenu.Registrar", "/com/canonical/AppMenu/Registrar",
        "com.canonical.AppMenu.Registrar", "RegisterWindow");
    uint32_t window = 77;
    const char* path = "/Menu";

    dbus_message_append_args(call, DBUS_TYPE_UINT32, &window,
                             DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
    DBusMessage* reply = blocking_call(call);

    if (reply) {
        dbus_message_unref(reply);
    }

    call = dbus_message_new_method_call(
        "com.canonical.AppMenu.Registrar", "/com/canonical/AppMenu/Registrar",
        "com.canonical.AppMenu.Registrar", "GetMenuForWindow");
    dbus_message_append_args(call, DBUS_TYPE_UINT32, &window, DBUS_TYPE_INVALID);
    reply = blocking_call(call);

    if (reply) {
        const char *service = "", *got_path = "";

        if (!dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &service,
                                   DBUS_TYPE_OBJECT_PATH, &got_path, DBUS_TYPE_INVALID) ||
            strcmp(service, dbus_bus_get_unique_name(bus)) || strcmp(got_path, "/Menu")) {
            failed = true;
        }

        dbus_message_unref(reply);
    }

    puts("registrar roundtrip");
}

static void wm_ping(void* data, struct xdg_wm_base* wm, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_listener = {wm_ping};

static void xdg_configure(void* data, struct xdg_surface* surface, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    configured = true;
}

static const struct xdg_surface_listener xdg_listener = {xdg_configure};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data;

    if (!strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 6 ? version : 6);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);
    } else if (!strcmp(interface, org_kde_kwin_appmenu_manager_interface.name)) {
        appmenu_manager = wl_registry_bind(
            registry, name, &org_kde_kwin_appmenu_manager_interface, version < 2 ? version : 2);
    }
}

static void registry_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove,
};

static void pump_once(void) {
    struct pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};

    wl_display_dispatch_pending(display);
    wl_display_flush(display);

    if (poll(&pfd, 1, 10) > 0 && (pfd.revents & POLLIN)) {
        if (wl_display_dispatch(display) < 0) {
            failed = true;
        }
    }

    dbus_connection_read_write_dispatch(bus, 0);
}

static struct wl_buffer* green_buffer(void) {
    const int width = 360, height = 220, stride = width * 4;
    int fd = memfd_create("global-menu-conform", MFD_CLOEXEC);

    if (fd < 0 || ftruncate(fd, stride * height) < 0) {
        return NULL;
    }

    uint32_t* pixels = mmap(NULL, stride * height, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (pixels == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    for (int i = 0; i < width * height; i++) {
        pixels[i] = 0xff20b060;
    }

    munmap(pixels, stride * height);

    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, stride * height);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buffer;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    DBusError error;

    dbus_error_init(&error);
    bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);

    if (!bus) {
        return 1;
    }

    dbus_connection_set_exit_on_disconnect(bus, FALSE);

    if (dbus_bus_request_name(bus, "org.example.ImwayGlobalMenuConform",
                              DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        return 2;
    }

    DBusObjectPathVTable vtable = {0};

    vtable.message_function = menu_message;
    dbus_connection_register_object_path(bus, "/Menu", &vtable, NULL);
    test_registrar();

    display = wl_display_connect(NULL);

    if (!display) {
        return 3;
    }

    struct wl_registry* registry = wl_display_get_registry(display);

    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !shm || !wm_base || !appmenu_manager) {
        fprintf(stderr, "missing required global\n");
        return 4;
    }

    struct wl_surface* surface = wl_compositor_create_surface(compositor);
    struct xdg_surface* xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);

    xdg_surface_add_listener(xdg_surface, &xdg_listener, NULL);

    struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdg_surface);

    xdg_toplevel_set_title(toplevel, "DBusMenu conform client");
    xdg_toplevel_set_app_id(toplevel, "dbusmenu-conform");

    struct org_kde_kwin_appmenu* appmenu = org_kde_kwin_appmenu_manager_create(appmenu_manager, surface);

    org_kde_kwin_appmenu_set_address(appmenu, "org.example.ImwayGlobalMenuConform", "/Menu");
    wl_surface_commit(surface);

    while (!configured && !failed) {
        pump_once();
    }

    struct wl_buffer* buffer = green_buffer();

    if (!buffer) {
        return 5;
    }

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(surface);
    puts("global menu mapped");
    mapped_printed = true;

    while (!failed) {
        pump_once();

        if (got_event) {
            // Leave enough time for the compositor to consume both signals.
            for (int i = 0; i < 30; i++) {
                pump_once();
            }

            puts("conform complete");

            return 0;
        }
    }

    return mapped_printed ? 6 : 7;
}
