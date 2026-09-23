/* A NetworkManager that misbehaves: the first device list is an error, the
 * next names a device that fails, two wifi devices and a PropertiesChanged
 * arriving while that refresh is still in flight; then saved connections
 * and access points that fail, lack their SSID or carry it mistyped; an
 * empty device list; property dicts keyed by integers; and finally a
 * wireless read it never answers. Launched from imway-pre so the name is
 * owned before the compositor probes for it; the scenario moves it from
 * one round to the next with go-files and reads what it was asked here. */
#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* conn;
static int round_no;
static int released;
static DBusMessage* held;

static const char* kNm = "org.freedesktop.NetworkManager";
static const char* kNmPath = "/org/freedesktop/NetworkManager";
static const char* kWlan0 = "/org/freedesktop/NetworkManager/Devices/0";
static const char* kWlan1 = "/org/freedesktop/NetworkManager/Devices/1";
static const char* kWlan2 = "/org/freedesktop/NetworkManager/Devices/2";
static const char* kAp[] = {
    "/org/freedesktop/NetworkManager/AccessPoint/1", "/org/freedesktop/NetworkManager/AccessPoint/2",
    "/org/freedesktop/NetworkManager/AccessPoint/3", "/org/freedesktop/NetworkManager/AccessPoint/4",
    "/org/freedesktop/NetworkManager/AccessPoint/5",
};
static const char* kAp9[] = {
    "/org/freedesktop/NetworkManager/AccessPoint/9",
    "/org/freedesktop/NetworkManager/AccessPoint/8",
};
static const char* kConn[] = {
    "/org/freedesktop/NetworkManager/Settings/1",
    "/org/freedesktop/NetworkManager/Settings/2",
    "/org/freedesktop/NetworkManager/Settings/3",
    "/org/freedesktop/NetworkManager/Settings/4",
    "/org/freedesktop/NetworkManager/Settings/5",
};
static const char* kConn9[] = {
    "/org/freedesktop/NetworkManager/Settings/9",
    "/org/freedesktop/NetworkManager/Settings/8",
};

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

static void var_string(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;

    open_entry(dict, key, "s", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    close_entry(dict, &entry, &var);
}

static void var_path(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;

    open_entry(dict, key, "o", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &value);
    close_entry(dict, &entry, &var);
}

static void var_ssid(DBusMessageIter* dict, const char* key, const char* ssid) {
    DBusMessageIter entry, var, arr;
    const unsigned char* p = (const unsigned char*)ssid;

    open_entry(dict, key, "ay", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &arr);

    if (ssid[0]) {
        dbus_message_iter_append_fixed_array(&arr, DBUS_TYPE_BYTE, &p, (int)strlen(ssid));
    }

    dbus_message_iter_close_container(&var, &arr);
    close_entry(dict, &entry, &var);
}

static void var_paths(DBusMessageIter* dict, const char* key, const char* const* paths, int count) {
    DBusMessageIter entry, var, arr;

    open_entry(dict, key, "ao", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "o", &arr);

    for (int i = 0; i < count; i++) {
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_OBJECT_PATH, &paths[i]);
    }

    dbus_message_iter_close_container(&var, &arr);
    close_entry(dict, &entry, &var);
}

/* a Get reply: one variant holding an object path array */
static void reply_paths(DBusMessage* call, const char* const* paths, int count) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, var, arr;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "ao", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "o", &arr);

    for (int i = 0; i < count; i++) {
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_OBJECT_PATH, &paths[i]);
    }

    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&it, &var);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

static void reply_error(DBusMessage* call) {
    DBusMessage* err = dbus_message_new_error(call, "org.freedesktop.NetworkManager.Failed", "refused");

    dbus_connection_send(conn, err, NULL);
    dbus_message_unref(err);
}

static void reply_text(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    const char* text = "not a dict";

    dbus_message_append_args(reply, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

/* a property dict keyed by integers where the names belong */
static void reply_int_keys(DBusMessage* call, const char* inner) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, dict, entry, value;
    uint32_t key = 0x7ffffff0;

    dbus_message_iter_init_append(reply, &it);

    if (inner) {
        DBusMessageIter props;

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{ua{sv}}", &dict);
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_UINT32, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sv}", &props);
        var_string(&props, inner, "x");
        dbus_message_iter_close_container(&entry, &props);
        dbus_message_iter_close_container(&dict, &entry);
    } else {
        const char* text = "x";

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{uv}", &dict);
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_UINT32, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &value);
        dbus_message_iter_append_basic(&value, DBUS_TYPE_STRING, &text);
        dbus_message_iter_close_container(&entry, &value);
        dbus_message_iter_close_container(&dict, &entry);
    }

    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

/* dicts whose values are plain strings, not variants; nested for the
 * settings group level */
static void reply_plain(DBusMessage* call, int nested) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, dict, entry, inner, pair;
    const char* key = nested ? "802-11-wireless" : "Ssid";
    const char* ssid = "ssid";
    const char* value = "plain";

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, nested ? "{sa{ss}}" : "{ss}", &dict);
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

    if (nested) {
        dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{ss}", &inner);
        dbus_message_iter_open_container(&inner, DBUS_TYPE_DICT_ENTRY, NULL, &pair);
        dbus_message_iter_append_basic(&pair, DBUS_TYPE_STRING, &ssid);
        dbus_message_iter_append_basic(&pair, DBUS_TYPE_STRING, &value);
        dbus_message_iter_close_container(&inner, &pair);
        dbus_message_iter_close_container(&entry, &inner);
    } else {
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &value);
    }

    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

static DBusMessage* dict_reply(DBusMessage* call, DBusMessageIter* it, DBusMessageIter* dict) {
    DBusMessage* reply = dbus_message_new_method_return(call);

    dbus_message_iter_init_append(reply, it);
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sv}", dict);

    return reply;
}

static void send_dict(DBusMessage* reply, DBusMessageIter* it, DBusMessageIter* dict) {
    dbus_message_iter_close_container(it, dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

static void emit_changed(void) {
    DBusMessage* sig = dbus_message_new_signal(kNmPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    DBusMessageIter it, props, inval;
    const char* iface = kNm;

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &props);
    dbus_message_iter_close_container(&it, &props);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);
    dbus_message_iter_close_container(&it, &inval);
    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
}

static void devices(DBusMessage* call) {
    round_no++;
    printf("devices %d\n", round_no);

    if (round_no == 1) {
        reply_error(call);
    } else if (round_no == 2) {
        const char* list[] = {kWlan1, kWlan0, kWlan2};

        /* a change arrives while this refresh is still in flight */
        emit_changed();
        usleep(300 * 1000);
        reply_paths(call, list, 3);
    } else if (round_no == 4) {
        reply_paths(call, NULL, 0);
    } else {
        reply_paths(call, &kWlan0, 1);
    }
}

static void get_all(DBusMessage* call, const char* path, const char* iface) {
    DBusMessageIter it, dict;
    DBusMessage* reply;

    printf("get-all %d %s %s\n", round_no, path, iface);

    if (!strcmp(path, kWlan1)) {
        reply_error(call);
    } else if (!strcmp(path, kWlan0) && !strcmp(iface, "org.freedesktop.NetworkManager.Device")) {
        reply = dict_reply(call, &it, &dict);
        var_u32(&dict, "DeviceType", 2);
        var_u32(&dict, "State", round_no == 2 ? 50 : 100);
        var_string(&dict, "Driver", "fake");
        send_dict(reply, &it, &dict);
    } else if (!strcmp(path, kWlan2)) {
        /* an answer with nothing in it */
        reply = dbus_message_new_method_return(call);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    } else if (!strcmp(path, kWlan0)) {
        if (round_no >= 6) {
            held = dbus_message_ref(call);
            puts("wireless held");

            return;
        }

        reply = dict_reply(call, &it, &dict);

        if (round_no == 3) {
            var_path(&dict, "ActiveAccessPoint", kAp[0]);
            var_paths(&dict, "AccessPoints", kAp, 5);
        } else if (round_no == 5) {
            var_string(&dict, "AccessPoints", "none");
            var_paths(&dict, "AccessPoints", kAp9, 2);
        } else {
            var_paths(&dict, "AccessPoints", NULL, 0);
        }

        send_dict(reply, &it, &dict);
    } else if (!strcmp(path, kAp[0])) {
        reply = dict_reply(call, &it, &dict);
        var_ssid(&dict, "Ssid", "edge-one");
        var_byte(&dict, "Strength", 70);
        var_u32(&dict, "WpaFlags", 0);
        var_u32(&dict, "RsnFlags", 0);
        send_dict(reply, &it, &dict);
    } else if (!strcmp(path, kAp[1])) {
        reply_error(call);
    } else if (!strcmp(path, kAp[2])) {
        reply = dict_reply(call, &it, &dict);
        var_u32(&dict, "Strength", 5);
        send_dict(reply, &it, &dict);
    } else if (!strcmp(path, kAp[3])) {
        reply = dict_reply(call, &it, &dict);
        var_string(&dict, "Ssid", "not bytes");
        var_string(&dict, "WpaFlags", "none");
        send_dict(reply, &it, &dict);
    } else if (!strcmp(path, kAp[4])) {
        reply_text(call);
    } else if (!strcmp(path, kAp9[0])) {
        reply_int_keys(call, NULL);
    } else if (!strcmp(path, kAp9[1])) {
        reply_plain(call, 0);
    } else {
        reply_error(call);
    }
}

static void connections(DBusMessage* call) {
    printf("connections %d\n", round_no);

    if (round_no == 2) {
        reply_error(call);
    } else if (round_no == 3) {
        reply_paths(call, kConn, 5);
    } else if (round_no == 5) {
        reply_paths(call, kConn9, 2);
    } else if (round_no == 6) {
        reply_text(call);
    } else {
        reply_paths(call, NULL, 0);
    }
}

static void settings(DBusMessage* call, const char* path) {
    printf("settings %s\n", path);

    if (!strcmp(path, kConn[0])) {
        reply_error(call);
    } else if (!strcmp(path, kConn[1])) {
        /* a wireless group whose SSID is empty, beside another group */
        DBusMessage* reply = dbus_message_new_method_return(call);
        DBusMessageIter it, groups, grp, props;
        const char* names[] = {"connection", "802-11-wireless"};

        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sa{sv}}", &groups);

        for (int i = 0; i < 2; i++) {
            dbus_message_iter_open_container(&groups, DBUS_TYPE_DICT_ENTRY, NULL, &grp);
            dbus_message_iter_append_basic(&grp, DBUS_TYPE_STRING, &names[i]);
            dbus_message_iter_open_container(&grp, DBUS_TYPE_ARRAY, "{sv}", &props);

            if (i) {
                var_string(&props, "mode", "infrastructure");
                var_ssid(&props, "ssid", "");
            } else {
                var_string(&props, "id", "edge-empty");
            }

            dbus_message_iter_close_container(&grp, &props);
            dbus_message_iter_close_container(&groups, &grp);
        }

        dbus_message_iter_close_container(&it, &groups);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    } else if (!strcmp(path, kConn[3])) {
        /* settings with nothing in them */
        DBusMessage* reply = dbus_message_new_method_return(call);

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    } else if (!strcmp(path, kConn[4])) {
        /* the wireless group as a variant, not a dict */
        DBusMessage* reply = dbus_message_new_method_return(call);
        DBusMessageIter it, dict;

        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
        var_string(&dict, "802-11-wireless", "flat");
        dbus_message_iter_close_container(&it, &dict);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    } else if (!strcmp(path, kConn9[0])) {
        reply_int_keys(call, "ssid");
    } else if (!strcmp(path, kConn9[1])) {
        reply_plain(call, 1);
    } else {
        reply_text(call);
    }
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c;
    (void)data;

    const char* path = dbus_message_get_path(msg);

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        const char* iface = "";
        const char* prop = "";

        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

        if (!strcmp(prop, "Devices")) {
            devices(msg);
        } else if (!strcmp(prop, "Connections")) {
            connections(msg);
        } else {
            reply_error(msg);
        }
    } else if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        const char* iface = "";

        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
        get_all(msg, path, iface);
    } else if (dbus_message_is_method_call(msg, "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings")) {
        settings(msg, path);
    } else if (dbus_message_is_method_call(msg, "org.freedesktop.NetworkManager.Device.Wireless", "RequestScan")) {
        puts("scan requested");
        reply_error(msg);
    } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        reply_error(msg);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_connection_flush(conn);

    return DBUS_HANDLER_RESULT_HANDLED;
}

int main(void) {
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

    /* each go-file the scenario drops announces one more change */
    while (dbus_connection_read_write_dispatch(conn, 100)) {
        char go[512];

        snprintf(go, sizeof(go), "%s/go-%d", getenv("XDG_RUNTIME_DIR"), released + 1);

        if (access(go, F_OK) == 0) {
            released++;
            printf("change %d\n", released);
            emit_changed();
        }
    }

    return 0;
}
