#include <dbus/dbus.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* conn;

static void dict_string(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void dict_path(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &value);
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

static void dict_pixmap(DBusMessageIter* dict) {
    const char* key = "IconPixmap";
    DBusMessageIter entry, var, images, image, bytes;
    int32_t w = 16, h = 16;
    unsigned char pixels[16 * 16 * 4];

    for (int i = 0; i < 16 * 16; i++) {
        pixels[i * 4 + 0] = 255; // A
        pixels[i * 4 + 1] = 255; // R
        pixels[i * 4 + 2] = 0;   // G
        pixels[i * 4 + 3] = 255; // B
    }

    const unsigned char* p = pixels;
    int count = sizeof(pixels);

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(iiay)", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "(iiay)", &images);
    dbus_message_iter_open_container(&images, DBUS_TYPE_STRUCT, NULL, &image);
    dbus_message_iter_append_basic(&image, DBUS_TYPE_INT32, &w);
    dbus_message_iter_append_basic(&image, DBUS_TYPE_INT32, &h);
    dbus_message_iter_open_container(&image, DBUS_TYPE_ARRAY, "y", &bytes);
    dbus_message_iter_append_fixed_array(&bytes, DBUS_TYPE_BYTE, &p, count);
    dbus_message_iter_close_container(&image, &bytes);
    dbus_message_iter_close_container(&images, &image);
    dbus_message_iter_close_container(&var, &images);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void send_properties(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, dict;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dict_string(&dict, "Id", "dock-status-test");
    dict_string(&dict, "Title", "Dock status notifier test");
    dict_string(&dict, "DesktopEntry", "dock-status-test");
    dict_string(&dict, "Status", "Active");
    dict_path(&dict, "Menu", "/Menu");
    dict_bool(&dict, "ItemIsMenu", FALSE);
    dict_pixmap(&dict);
    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

static void append_menu_node(DBusMessageIter* parent, int32_t id, const char* label) {
    DBusMessageIter node, props, children;
    dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, NULL, &node);
    dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &props);
    if (label) dict_string(&props, "label", label);
    dbus_message_iter_close_container(&node, &props);
    dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "v", &children);
    dbus_message_iter_close_container(&node, &children);
    dbus_message_iter_close_container(parent, &node);
}

static void send_layout(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, root, props, children, child_var;
    uint32_t revision = 1;
    int32_t root_id = 0;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &revision);
    dbus_message_iter_open_container(&it, DBUS_TYPE_STRUCT, NULL, &root);
    dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &root_id);
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}", &props);
    dbus_message_iter_close_container(&root, &props);
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "v", &children);
    dbus_message_iter_open_container(&children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &child_var);
    append_menu_node(&child_var, 7, "A very visible tray action");
    dbus_message_iter_close_container(&children, &child_var);
    dbus_message_iter_close_container(&root, &children);
    dbus_message_iter_close_container(&it, &root);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    puts("layout requested");
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c; (void)data;
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        send_properties(msg);
    } else if (dbus_message_is_method_call(msg, "org.kde.StatusNotifierItem", "Activate")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        puts("activated");
    } else if (dbus_message_is_method_call(msg, "org.kde.StatusNotifierItem", "ContextMenu")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        puts("context");
    } else if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "GetLayout")) {
        send_layout(msg);
    } else if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "AboutToShow")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_bool_t update = FALSE;
        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &update, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    } else if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "Event")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        puts("menu clicked");
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    dbus_connection_flush(conn);
    return DBUS_HANDLER_RESULT_HANDLED;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    DBusError err;
    dbus_error_init(&err);
    conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!conn) return 1;
    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    if (dbus_bus_request_name(conn, "org.example.ImwayDockStatusTest",
            DBUS_NAME_FLAG_DO_NOT_QUEUE, &err) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) return 2;

    DBusObjectPathVTable vt = {0};
    vt.message_function = message;
    dbus_connection_register_object_path(conn, "/StatusNotifierItem", &vt, NULL);
    dbus_connection_register_object_path(conn, "/Menu", &vt, NULL);

    DBusMessage* call = dbus_message_new_method_call(
        "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
    const char* service = "org.example.ImwayDockStatusTest";
    dbus_message_append_args(call, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 3000, &err);
    dbus_message_unref(call);
    if (!reply) return 3;
    dbus_message_unref(reply);
    puts("registered");

    while (dbus_connection_read_write_dispatch(conn, 1000)) {
    }
    return 0;
}
