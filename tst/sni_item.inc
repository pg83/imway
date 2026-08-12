/* A minimal StatusNotifierItem: dict helpers, an ARGB pixmap whose color the
 * test can change, the Properties.GetAll reply and watcher registration.
 * Shared by the tray feature and regression clients. */
#include <dbus/dbus.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* conn;
/* the pixmap color served to GetAll; tests repaint it and signal NewIcon */
static unsigned char sni_r = 255, sni_g = 0, sni_b = 255;

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
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = sni_r;
        pixels[i * 4 + 2] = sni_g;
        pixels[i * 4 + 3] = sni_b;
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
    dict_string(&dict, "Status", "Active");
    dict_string(&dict, "Category", "ApplicationStatus");
    dict_pixmap(&dict);
    dict_path(&dict, "Menu", "/Menu");
    dict_bool(&dict, "ItemIsMenu", FALSE);
    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

static void sni_new_icon(void) {
    DBusMessage* sig = dbus_message_new_signal(
        "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewIcon");
    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
}

/* connect, own bus_name, register the object paths on vt, announce to the
 * watcher; returns 0 and prints "registered" on success */
static int sni_register(const char* bus_name, const DBusObjectPathVTable* vt) {
    DBusError err;
    dbus_error_init(&err);
    conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!conn) return 1;
    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    if (dbus_bus_request_name(conn, bus_name,
            DBUS_NAME_FLAG_DO_NOT_QUEUE, &err) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) return 2;

    dbus_connection_register_object_path(conn, "/StatusNotifierItem", vt, NULL);
    dbus_connection_register_object_path(conn, "/Menu", vt, NULL);

    DBusMessage* call = dbus_message_new_method_call(
        "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
    dbus_message_append_args(call, DBUS_TYPE_STRING, &bus_name, DBUS_TYPE_INVALID);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 3000, &err);
    dbus_message_unref(call);
    if (!reply) return 3;
    dbus_message_unref(reply);
    puts("registered");
    return 0;
}
