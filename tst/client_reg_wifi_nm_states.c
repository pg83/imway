/* A NetworkManager with one wifi device whose state the scenario sets: it
 * writes the NM device state number into ./state and touches ./go-N, and
 * this fake answers with that state and announces a change. One access
 * point, active, is always in range. Launched from imway-pre so the name
 * is owned before the compositor probes for it. */
#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* conn;

static const char* kNm = "org.freedesktop.NetworkManager";
static const char* kNmPath = "/org/freedesktop/NetworkManager";
static const char* kDev = "/org/freedesktop/NetworkManager/Devices/0";
static const char* kAp = "/org/freedesktop/NetworkManager/AccessPoint/1";

static uint32_t device_state(void) {
    char path[512];
    unsigned state = 30;

    snprintf(path, sizeof(path), "%s/state", getenv("XDG_RUNTIME_DIR"));

    FILE* f = fopen(path, "r");

    if (f) {
        if (fscanf(f, "%u", &state) != 1) {
            state = 30;
        }

        fclose(f);
    }

    return state;
}

static void open_entry(DBusMessageIter* dict, const char* key, const char* sig, DBusMessageIter* entry,
                       DBusMessageIter* var) {
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, entry);
    dbus_message_iter_append_basic(entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(entry, DBUS_TYPE_VARIANT, sig, var);
}

static void close_entry(DBusMessageIter* dict, DBusMessageIter* entry, DBusMessageIter* var) {
    dbus_message_iter_close_container(entry, var);
    dbus_message_iter_close_container(dict, entry);
}

static void var_u32(DBusMessageIter* dict, const char* key, uint32_t value) {
    DBusMessageIter entry, var;

    open_entry(dict, key, "u", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &value);
    close_entry(dict, &entry, &var);
}

static void var_byte(DBusMessageIter* dict, const char* key, uint8_t value) {
    DBusMessageIter entry, var;

    open_entry(dict, key, "y", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BYTE, &value);
    close_entry(dict, &entry, &var);
}

static void var_path(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;

    open_entry(dict, key, "o", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &value);
    close_entry(dict, &entry, &var);
}

static void var_paths(DBusMessageIter* dict, const char* key, const char* path) {
    DBusMessageIter entry, var, arr;

    open_entry(dict, key, "ao", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "o", &arr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_close_container(&var, &arr);
    close_entry(dict, &entry, &var);
}

static void var_ssid(DBusMessageIter* dict, const char* ssid) {
    DBusMessageIter entry, var, arr;
    const unsigned char* p = (const unsigned char*)ssid;

    open_entry(dict, "Ssid", "ay", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &arr);
    dbus_message_iter_append_fixed_array(&arr, DBUS_TYPE_BYTE, &p, (int)strlen(ssid));
    dbus_message_iter_close_container(&var, &arr);
    close_entry(dict, &entry, &var);
}

/* a Get reply: one variant holding an object path array of at most one */
static void reply_paths(DBusMessage* call, const char* path) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, var, arr;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "ao", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "o", &arr);

    if (path) {
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_OBJECT_PATH, &path);
    }

    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&it, &var);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

static void get_all(DBusMessage* call, const char* path, const char* iface) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, dict;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);

    if (!strcmp(path, kDev) && !strcmp(iface, "org.freedesktop.NetworkManager.Device")) {
        uint32_t state = device_state();

        printf("state %u\n", state);
        var_u32(&dict, "DeviceType", 2);
        var_u32(&dict, "State", state);
    } else if (!strcmp(path, kDev)) {
        var_path(&dict, "ActiveAccessPoint", kAp);
        var_paths(&dict, "AccessPoints", kAp);
    } else if (!strcmp(path, kAp)) {
        var_ssid(&dict, "states");
        var_byte(&dict, "Strength", 60);
        var_u32(&dict, "WpaFlags", 0);
        var_u32(&dict, "RsnFlags", 0);
    }

    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c;
    (void)data;

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        const char* iface = "";
        const char* prop = "";

        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
        reply_paths(msg, !strcmp(prop, "Devices") ? kDev : NULL);
    } else if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        const char* iface = "";

        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
        get_all(msg, dbus_message_get_path(msg), iface);
    } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        DBusMessage* err = dbus_message_new_error(msg, "org.freedesktop.NetworkManager.Failed", "not faked");

        dbus_connection_send(conn, err, NULL);
        dbus_message_unref(err);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_connection_flush(conn);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void emit_changed(void) {
    DBusMessage* sig = dbus_message_new_signal(kNmPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    DBusMessageIter it, props, inval;

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &kNm);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &props);
    dbus_message_iter_close_container(&it, &props);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);
    dbus_message_iter_close_container(&it, &inval);
    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
}

int main(void) {
    int released = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(120);

    conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, NULL);

    if (!conn) {
        return 1;
    }

    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    if (dbus_bus_request_name(conn, kNm, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        return 2;
    }

    DBusObjectPathVTable vt = {0};

    vt.message_function = message;
    dbus_connection_register_fallback(conn, "/", &vt, NULL);
    puts("nm ready");

    while (dbus_connection_read_write_dispatch(conn, 50)) {
        char go[512];

        snprintf(go, sizeof(go), "%s/go-%d", getenv("XDG_RUNTIME_DIR"), released + 1);

        if (access(go, F_OK) == 0) {
            released++;
            emit_changed();
        }
    }

    return 0;
}
