/* A fake NetworkManager on the (aliased) system bus: one wifi device and one
 * wired one, three access points — a known secured network, an open one and
 * an unknown secured one — and one saved connection. Activation requests
 * flip the device to activated and announce it with PropertiesChanged, so
 * the compositor's refresh tree, its glyph and its notifications follow.
 * Launched from imway-pre so the name is owned before the compositor probes
 * for it; everything it is asked is logged for the scenario. */
#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* conn;

static const char* kNm = "org.freedesktop.NetworkManager";
static const char* kNmPath = "/org/freedesktop/NetworkManager";
static const char* kSettings = "org.freedesktop.NetworkManager.Settings";
static const char* kSettingsPath = "/org/freedesktop/NetworkManager/Settings";
static const char* kDev = "org.freedesktop.NetworkManager.Device";
static const char* kWireless = "org.freedesktop.NetworkManager.Device.Wireless";
static const char* kAp = "org.freedesktop.NetworkManager.AccessPoint";
static const char* kConn = "org.freedesktop.NetworkManager.Settings.Connection";

static const char* kWlan = "/org/freedesktop/NetworkManager/Devices/1";
static const char* kEth = "/org/freedesktop/NetworkManager/Devices/2";
static const char* kApKnown = "/org/freedesktop/NetworkManager/AccessPoint/1";
static const char* kApOpen = "/org/freedesktop/NetworkManager/AccessPoint/2";
static const char* kApSecured = "/org/freedesktop/NetworkManager/AccessPoint/3";
static const char* kSaved = "/org/freedesktop/NetworkManager/Settings/1";

static uint32_t wlan_state = 30; /* NM_DEVICE_STATE_DISCONNECTED */
static const char* active_ap = "/";

static void open_entry(DBusMessageIter* dict, const char* key, const char* sig,
                       DBusMessageIter* entry, DBusMessageIter* var) {
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

static void var_string(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;
    open_entry(dict, key, "s", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    close_entry(dict, &entry, &var);
}

static void var_paths(DBusMessageIter* dict, const char* key, const char* const* paths, int count) {
    DBusMessageIter entry, var, arr;
    open_entry(dict, key, "ao", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "o", &arr);
    for (int i = 0; i < count; i++) dbus_message_iter_append_basic(&arr, DBUS_TYPE_OBJECT_PATH, &paths[i]);
    dbus_message_iter_close_container(&var, &arr);
    close_entry(dict, &entry, &var);
}

static void var_ssid(DBusMessageIter* dict, const char* key, const char* ssid) {
    DBusMessageIter entry, var, arr;
    open_entry(dict, key, "ay", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &arr);
    for (const char* p = ssid; *p; p++) dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, p);
    dbus_message_iter_close_container(&var, &arr);
    close_entry(dict, &entry, &var);
}

/* the property dictionary of one interface on one object */
static int fill_props(DBusMessageIter* dict, const char* path, const char* iface) {
    if (!strcmp(path, kNmPath) && !strcmp(iface, kNm)) {
        const char* devices[] = {kEth, kWlan};
        var_paths(dict, "Devices", devices, 2);
        return 1;
    }
    if (!strcmp(path, kSettingsPath) && !strcmp(iface, kSettings)) {
        const char* conns[] = {kSaved};
        var_paths(dict, "Connections", conns, 1);
        return 1;
    }
    if (!strcmp(path, kEth) && !strcmp(iface, kDev)) {
        var_u32(dict, "DeviceType", 1);
        var_u32(dict, "State", 100);
        return 1;
    }
    if (!strcmp(path, kWlan) && !strcmp(iface, kDev)) {
        var_u32(dict, "DeviceType", 2);
        var_u32(dict, "State", wlan_state);
        return 1;
    }
    if (!strcmp(path, kWlan) && !strcmp(iface, kWireless)) {
        const char* aps[] = {kApKnown, kApOpen, kApSecured};
        var_path(dict, "ActiveAccessPoint", active_ap);
        var_paths(dict, "AccessPoints", aps, 3);
        return 1;
    }
    if (!strcmp(path, kApKnown) && !strcmp(iface, kAp)) {
        var_ssid(dict, "Ssid", "imway-known");
        var_byte(dict, "Strength", 80);
        var_u32(dict, "WpaFlags", 0);
        var_u32(dict, "RsnFlags", 0x100);
        return 1;
    }
    if (!strcmp(path, kApOpen) && !strcmp(iface, kAp)) {
        var_ssid(dict, "Ssid", "imway-open");
        var_byte(dict, "Strength", 40);
        var_u32(dict, "WpaFlags", 0);
        var_u32(dict, "RsnFlags", 0);
        return 1;
    }
    if (!strcmp(path, kApSecured) && !strcmp(iface, kAp)) {
        var_ssid(dict, "Ssid", "imway-secured");
        var_byte(dict, "Strength", 60);
        var_u32(dict, "WpaFlags", 0x200);
        var_u32(dict, "RsnFlags", 0x200);
        return 1;
    }
    return 0;
}

static void reply_get_all(DBusMessage* call, const char* path, const char* iface) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, dict;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    fill_props(&dict, path, iface);
    dbus_message_iter_close_container(&it, &dict);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    printf("get-all %s %s\n", path, iface);
}

/* Get reads a single property: reuse the dictionary builder and pick the
 * entry out of it, so every property has exactly one definition above */
static void reply_get(DBusMessage* call, const char* path, const char* iface, const char* prop) {
    DBusMessage* all = dbus_message_new_method_return(call);
    DBusMessageIter it, dict;

    dbus_message_iter_init_append(all, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    fill_props(&dict, path, iface);
    dbus_message_iter_close_container(&it, &dict);

    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter out, in, entries;
    int found = 0;

    dbus_message_iter_init_append(reply, &out);
    dbus_message_iter_init(all, &in);
    dbus_message_iter_recurse(&in, &entries);

    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        const char* key = "";

        dbus_message_iter_recurse(&entries, &entry);
        dbus_message_iter_get_basic(&entry, &key);

        if (!strcmp(key, prop)) {
            DBusMessageIter var, copy;
            char* sig;

            dbus_message_iter_next(&entry);
            dbus_message_iter_recurse(&entry, &var);
            sig = dbus_message_iter_get_signature(&var);
            dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, sig, &copy);

            if (!strcmp(sig, "ao")) {
                DBusMessageIter src, dst;
                dbus_message_iter_recurse(&var, &src);
                dbus_message_iter_open_container(&copy, DBUS_TYPE_ARRAY, "o", &dst);
                while (dbus_message_iter_get_arg_type(&src) == DBUS_TYPE_OBJECT_PATH) {
                    const char* p;
                    dbus_message_iter_get_basic(&src, &p);
                    dbus_message_iter_append_basic(&dst, DBUS_TYPE_OBJECT_PATH, &p);
                    dbus_message_iter_next(&src);
                }
                dbus_message_iter_close_container(&copy, &dst);
            } else if (!strcmp(sig, "u")) {
                uint32_t v;
                dbus_message_iter_get_basic(&var, &v);
                dbus_message_iter_append_basic(&copy, DBUS_TYPE_UINT32, &v);
            } else if (!strcmp(sig, "o") || !strcmp(sig, "s")) {
                const char* v;
                dbus_message_iter_get_basic(&var, &v);
                dbus_message_iter_append_basic(&copy, sig[0] == 'o' ? DBUS_TYPE_OBJECT_PATH : DBUS_TYPE_STRING, &v);
            }

            dbus_message_iter_close_container(&out, &copy);
            dbus_free(sig);
            found = 1;
            break;
        }

        dbus_message_iter_next(&entries);
    }

    dbus_message_unref(all);

    if (!found) {
        dbus_message_unref(reply);
        reply = dbus_message_new_error(call, DBUS_ERROR_UNKNOWN_PROPERTY, "not faked");
    }

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    printf("get %s %s %s\n", path, iface, prop);
}

static void reply_settings(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, groups, group, props;
    const char* g;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sa{sv}}", &groups);

    g = "connection";
    dbus_message_iter_open_container(&groups, DBUS_TYPE_DICT_ENTRY, NULL, &group);
    dbus_message_iter_append_basic(&group, DBUS_TYPE_STRING, &g);
    dbus_message_iter_open_container(&group, DBUS_TYPE_ARRAY, "{sv}", &props);
    var_string(&props, "id", "imway-known");
    var_string(&props, "type", "802-11-wireless");
    dbus_message_iter_close_container(&group, &props);
    dbus_message_iter_close_container(&groups, &group);

    g = "802-11-wireless";
    dbus_message_iter_open_container(&groups, DBUS_TYPE_DICT_ENTRY, NULL, &group);
    dbus_message_iter_append_basic(&group, DBUS_TYPE_STRING, &g);
    dbus_message_iter_open_container(&group, DBUS_TYPE_ARRAY, "{sv}", &props);
    var_ssid(&props, "ssid", "imway-known");
    var_string(&props, "mode", "infrastructure");
    dbus_message_iter_close_container(&group, &props);
    dbus_message_iter_close_container(&groups, &group);

    dbus_message_iter_close_container(&it, &groups);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    puts("settings served");
}

static void signal_device_changed(void) {
    DBusMessage* sig = dbus_message_new_signal(kWlan, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    DBusMessageIter it, props, inval;

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &kDev);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &props);
    var_u32(&props, "State", wlan_state);
    dbus_message_iter_close_container(&it, &props);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);
    dbus_message_iter_close_container(&it, &inval);
    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
}

static void reply_empty(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

/* the ssid and psk out of AddAndActivateConnection's settings */
static void read_add_settings(DBusMessage* msg, char* ssid, size_t ssid_cap, char* psk, size_t psk_cap) {
    DBusMessageIter it, groups;

    ssid[0] = psk[0] = 0;
    if (!dbus_message_iter_init(msg, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) return;
    dbus_message_iter_recurse(&it, &groups);

    while (dbus_message_iter_get_arg_type(&groups) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter group, props;
        const char* g = "";

        dbus_message_iter_recurse(&groups, &group);
        dbus_message_iter_get_basic(&group, &g);
        dbus_message_iter_next(&group);
        dbus_message_iter_recurse(&group, &props);

        while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter kv, var;
            const char* key = "";

            dbus_message_iter_recurse(&props, &kv);
            dbus_message_iter_get_basic(&kv, &key);
            dbus_message_iter_next(&kv);
            dbus_message_iter_recurse(&kv, &var);

            if (!strcmp(g, "802-11-wireless") && !strcmp(key, "ssid")) {
                DBusMessageIter bytes;
                size_t n = 0;
                dbus_message_iter_recurse(&var, &bytes);
                while (dbus_message_iter_get_arg_type(&bytes) == DBUS_TYPE_BYTE && n + 1 < ssid_cap) {
                    uint8_t b;
                    dbus_message_iter_get_basic(&bytes, &b);
                    ssid[n++] = (char)b;
                    dbus_message_iter_next(&bytes);
                }
                ssid[n] = 0;
            } else if (!strcmp(g, "802-11-wireless-security") && !strcmp(key, "psk")) {
                const char* v = "";
                dbus_message_iter_get_basic(&var, &v);
                snprintf(psk, psk_cap, "%s", v);
            }

            dbus_message_iter_next(&props);
        }

        dbus_message_iter_next(&groups);
    }
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c; (void)data;
    const char* path = dbus_message_get_path(msg);

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        const char* iface = "";
        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
        reply_get_all(msg, path, iface);
    } else if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        const char* iface = "";
        const char* prop = "";
        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
        reply_get(msg, path, iface, prop);
    } else if (dbus_message_is_method_call(msg, kConn, "GetSettings")) {
        reply_settings(msg);
    } else if (dbus_message_is_method_call(msg, kWireless, "RequestScan")) {
        reply_empty(msg);
        puts("scan requested");
    } else if (dbus_message_is_method_call(msg, kNm, "ActivateConnection")) {
        const char* connection = "";
        const char* device = "";
        const char* ap = "";
        dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &connection, DBUS_TYPE_OBJECT_PATH, &device, DBUS_TYPE_OBJECT_PATH, &ap, DBUS_TYPE_INVALID);
        reply_empty(msg);
        wlan_state = 100;
        active_ap = !strcmp(ap, kApOpen) ? kApOpen : kApKnown;
        printf("activate %s %s\n", connection, ap);
        signal_device_changed();
    } else if (dbus_message_is_method_call(msg, kNm, "AddAndActivateConnection")) {
        char ssid[64], psk[64];
        read_add_settings(msg, ssid, sizeof(ssid), psk, sizeof(psk));
        reply_empty(msg);
        wlan_state = 100;
        active_ap = kApSecured;
        printf("add-activate %s psk=%s\n", ssid, psk);
        signal_device_changed();
    } else if (dbus_message_is_method_call(msg, kDev, "Disconnect")) {
        reply_empty(msg);
        wlan_state = 30;
        active_ap = "/";
        puts("disconnected");
        signal_device_changed();
    } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        DBusMessage* err = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "not faked");
        dbus_connection_send(conn, err, NULL);
        dbus_message_unref(err);
        printf("unknown %s.%s\n", dbus_message_get_interface(msg), dbus_message_get_member(msg));
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
    if (!conn) return 1;
    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    if (dbus_bus_request_name(conn, kNm, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) return 2;

    DBusObjectPathVTable vt = {0};
    vt.message_function = message;
    dbus_connection_register_fallback(conn, "/", &vt, NULL);

    puts("nm ready");

    while (dbus_connection_read_write_dispatch(conn, 200)) {
    }
    return 0;
}
