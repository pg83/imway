/* A fake iwd on the (aliased) system bus: one station, two networks. Starts
 * disconnected, flips to connected two seconds after the first ordered-list
 * request and announces it with PropertiesChanged — the compositor's wifi
 * glyph must follow. Launched from imway-pre so the name is owned before
 * the compositor probes for it. */
#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static DBusConnection* conn;
static const char* kStation = "/dev0";
static const char* kNetA = "/dev0/net_a";
static const char* kNetB = "/dev0/net_b";
static int connected;
static time_t ordered_at;

static void var_string(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void var_bool(DBusMessageIter* dict, const char* key, dbus_bool_t value) {
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

/* one object entry: path + one interface with its property dict */
static void begin_object(DBusMessageIter* objs, const char* path, const char* iface,
                         DBusMessageIter* entry, DBusMessageIter* ifaces,
                         DBusMessageIter* ifentry, DBusMessageIter* props) {
    dbus_message_iter_open_container(objs, DBUS_TYPE_DICT_ENTRY, NULL, entry);
    dbus_message_iter_append_basic(entry, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_open_container(entry, DBUS_TYPE_ARRAY, "{sa{sv}}", ifaces);
    dbus_message_iter_open_container(ifaces, DBUS_TYPE_DICT_ENTRY, NULL, ifentry);
    dbus_message_iter_append_basic(ifentry, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(ifentry, DBUS_TYPE_ARRAY, "{sv}", props);
}

static void end_object(DBusMessageIter* objs, DBusMessageIter* entry, DBusMessageIter* ifaces,
                       DBusMessageIter* ifentry, DBusMessageIter* props) {
    dbus_message_iter_close_container(ifentry, props);
    dbus_message_iter_close_container(ifaces, ifentry);
    dbus_message_iter_close_container(entry, ifaces);
    dbus_message_iter_close_container(objs, entry);
}

static void send_managed(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, objs, entry, ifaces, ifentry, props;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{oa{sa{sv}}}", &objs);

    begin_object(&objs, kStation, "net.connman.iwd.Station", &entry, &ifaces, &ifentry, &props);
    var_string(&props, "State", connected ? "connected" : "disconnected");
    end_object(&objs, &entry, &ifaces, &ifentry, &props);

    begin_object(&objs, kNetA, "net.connman.iwd.Network", &entry, &ifaces, &ifentry, &props);
    var_string(&props, "Name", "imway-net-a");
    var_string(&props, "Type", "psk");
    var_bool(&props, "Connected", connected ? TRUE : FALSE);
    end_object(&objs, &entry, &ifaces, &ifentry, &props);

    begin_object(&objs, kNetB, "net.connman.iwd.Network", &entry, &ifaces, &ifentry, &props);
    var_string(&props, "Name", "imway-net-b");
    var_string(&props, "Type", "psk");
    var_bool(&props, "Connected", FALSE);
    end_object(&objs, &entry, &ifaces, &ifentry, &props);

    dbus_message_iter_close_container(&it, &objs);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    puts("managed objects served");
}

static void send_ordered(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, arr, e;
    int16_t strong = -5000, weak = -7500; /* dBm * 100 */

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(on)", &arr);
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_OBJECT_PATH, &kNetA);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_INT16, &strong);
    dbus_message_iter_close_container(&arr, &e);
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_OBJECT_PATH, &kNetB);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_INT16, &weak);
    dbus_message_iter_close_container(&arr, &e);
    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    if (!ordered_at) ordered_at = time(NULL);
    puts("ordered served");
}

static void signal_state_change(void) {
    DBusMessage* sig = dbus_message_new_signal(
        kStation, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    DBusMessageIter it, props, inval;
    const char* iface = "net.connman.iwd.Station";

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &props);
    var_string(&props, "State", "connected");
    dbus_message_iter_close_container(&it, &props);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);
    dbus_message_iter_close_container(&it, &inval);
    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c; (void)data;
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects")) {
        send_managed(msg);
    } else if (dbus_message_is_method_call(msg, "net.connman.iwd.Station", "GetOrderedNetworks")) {
        send_ordered(msg);
    } else if (dbus_message_is_method_call(msg, "net.connman.iwd.AgentManager", "RegisterAgent")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        puts("agent registered");
    } else if (dbus_message_is_method_call(msg, "net.connman.iwd.Network", "Connect")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        puts("connect called");
    } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        DBusMessage* err = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "not faked");
        dbus_connection_send(conn, err, NULL);
        dbus_message_unref(err);
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
    if (dbus_bus_request_name(conn, "net.connman.iwd",
            DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) return 2;

    DBusObjectPathVTable vt = {0};
    vt.message_function = message;
    dbus_connection_register_fallback(conn, "/", &vt, NULL);

    puts("iwd ready");

    /* flip to connected a beat after the compositor first pulled the list */
    while (dbus_connection_read_write_dispatch(conn, 200)) {
        if (!connected && ordered_at && time(NULL) - ordered_at >= 2) {
            connected = 1;
            signal_state_change();
            puts("flipped connected");
        }
    }
    return 0;
}
