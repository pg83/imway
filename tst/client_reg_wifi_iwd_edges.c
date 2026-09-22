/* An iwd that misbehaves. Its object tree first fails, then arrives while
 * a change is already pending, carries an interface the compositor does
 * not track, mistyped network properties and an ordered list naming a
 * network it never listed; then comes in every shape a dict can be
 * mistyped (integer object paths, integer interface names, plain-string
 * properties, non-dict interface and object values), and an ordered list
 * with integer paths. Its agent calls ask twice for one passphrase, cancel,
 * release, call a method no agent has and signal on the agent's path;
 * finally a passphrase request and an object read are left pending into
 * the compositor's shutdown. Launched from imway-pre so the name is owned
 * before the compositor probes; go-files from the scenario move it on. */
#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* conn;
static int round_no;
static char agent_path[256];
static char agent_dest[256];

static const char* kStation = "/dev0";
static const char* kNet1 = "/dev0/n1";
static const char* kNet2 = "/dev0/n2";
static const char* kNet3 = "/dev0/n3";

static void var_string(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void var_int(DBusMessageIter* dict, const char* key, int32_t value) {
    DBusMessageIter entry, var;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &value);
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

static void var_path(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

/* one object with one interface; returns the open property dict */
static void object_open(DBusMessageIter* objs, const char* path, const char* iface, DBusMessageIter* entry,
                        DBusMessageIter* ifaces, DBusMessageIter* ifentry, DBusMessageIter* props) {
    dbus_message_iter_open_container(objs, DBUS_TYPE_DICT_ENTRY, NULL, entry);
    dbus_message_iter_append_basic(entry, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_open_container(entry, DBUS_TYPE_ARRAY, "{sa{sv}}", ifaces);
    dbus_message_iter_open_container(ifaces, DBUS_TYPE_DICT_ENTRY, NULL, ifentry);
    dbus_message_iter_append_basic(ifentry, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(ifentry, DBUS_TYPE_ARRAY, "{sv}", props);
}

static void object_close(DBusMessageIter* objs, DBusMessageIter* entry, DBusMessageIter* ifaces,
                         DBusMessageIter* ifentry, DBusMessageIter* props) {
    dbus_message_iter_close_container(ifentry, props);
    dbus_message_iter_close_container(ifaces, ifentry);
    dbus_message_iter_close_container(entry, ifaces);
    dbus_message_iter_close_container(objs, entry);
}

static void reply_error(DBusMessage* call) {
    DBusMessage* err = dbus_message_new_error(call, "net.connman.iwd.Failed", "refused");

    dbus_connection_send(conn, err, NULL);
    dbus_message_unref(err);
}

static void emit_changed(const char* path) {
    DBusMessage* sig = dbus_message_new_signal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    DBusMessageIter it, props, inval;
    const char* iface = "net.connman.iwd.Station";

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

/* the well-formed tree of rounds 2, 3 and 9 */
static void tree(DBusMessage* reply, const char* state, int connected) {
    DBusMessageIter it, objs, entry, ifaces, ifentry, props;

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{oa{sa{sv}}}", &objs);

    object_open(&objs, kStation, "net.connman.iwd.Station", &entry, &ifaces, &ifentry, &props);
    var_string(&props, "State", state);
    object_close(&objs, &entry, &ifaces, &ifentry, &props);

    object_open(&objs, "/dev0/phy", "net.connman.iwd.Device", &entry, &ifaces, &ifentry, &props);
    var_string(&props, "Name", "wlan0");
    object_close(&objs, &entry, &ifaces, &ifentry, &props);

    object_open(&objs, kNet1, "net.connman.iwd.Network", &entry, &ifaces, &ifentry, &props);
    var_string(&props, "Name", "iwd-one");
    var_string(&props, "Type", "psk");
    var_bool(&props, "Connected", connected);
    var_path(&props, "KnownNetwork", "/known/1");
    object_close(&objs, &entry, &ifaces, &ifentry, &props);

    object_open(&objs, kNet2, "net.connman.iwd.Network", &entry, &ifaces, &ifentry, &props);
    var_int(&props, "Name", 2);
    var_string(&props, "Type", "open");
    var_string(&props, "Connected", "yes");
    var_int(&props, "KnownNetwork", 0);
    object_close(&objs, &entry, &ifaces, &ifentry, &props);

    object_open(&objs, kNet3, "net.connman.iwd.Network", &entry, &ifaces, &ifentry, &props);
    object_close(&objs, &entry, &ifaces, &ifentry, &props);

    dbus_message_iter_close_container(&it, &objs);
}

/* a string-keyed dict whose values are strings, standing in for the level
 * the compositor expects to be a dict of dicts or of variants */
static void plain_dict(DBusMessageIter* parent, const char* key, const char* value) {
    DBusMessageIter dict, entry;

    dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, "{ss}", &dict);
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(parent, &dict);
}

/* rounds 4..8: the tree mistyped at each level in turn */
static void mistyped(DBusMessage* reply, int shape) {
    DBusMessageIter it, objs, entry, ifaces, ifentry;
    uint32_t number = 0x7ffffff0;
    const char* station = "net.connman.iwd.Station";

    dbus_message_iter_init_append(reply, &it);

    if (shape == 0) {
        DBusMessageIter props;

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{ua{sa{sv}}}", &objs);
        dbus_message_iter_open_container(&objs, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_UINT32, &number);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);
        dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &ifentry);
        dbus_message_iter_append_basic(&ifentry, DBUS_TYPE_STRING, &station);
        dbus_message_iter_open_container(&ifentry, DBUS_TYPE_ARRAY, "{sv}", &props);
        var_string(&props, "State", "connected");
        dbus_message_iter_close_container(&ifentry, &props);
        dbus_message_iter_close_container(&ifaces, &ifentry);
        dbus_message_iter_close_container(&entry, &ifaces);
        dbus_message_iter_close_container(&objs, &entry);
    } else if (shape == 1) {
        DBusMessageIter props;

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{oa{ua{sv}}}", &objs);
        dbus_message_iter_open_container(&objs, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH, &kStation);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{ua{sv}}", &ifaces);
        dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &ifentry);
        dbus_message_iter_append_basic(&ifentry, DBUS_TYPE_UINT32, &number);
        dbus_message_iter_open_container(&ifentry, DBUS_TYPE_ARRAY, "{sv}", &props);
        var_string(&props, "State", "connected");
        dbus_message_iter_close_container(&ifentry, &props);
        dbus_message_iter_close_container(&ifaces, &ifentry);
        dbus_message_iter_close_container(&entry, &ifaces);
        dbus_message_iter_close_container(&objs, &entry);
    } else if (shape == 2) {
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{oa{sa{ss}}}", &objs);
        dbus_message_iter_open_container(&objs, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH, &kNet1);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sa{ss}}", &ifaces);
        dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &ifentry);
        station = "net.connman.iwd.Network";
        dbus_message_iter_append_basic(&ifentry, DBUS_TYPE_STRING, &station);
        plain_dict(&ifentry, "Name", "plain");
        dbus_message_iter_close_container(&ifaces, &ifentry);
        dbus_message_iter_close_container(&entry, &ifaces);
        dbus_message_iter_close_container(&objs, &entry);
    } else if (shape == 3) {
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{oa{ss}}", &objs);
        dbus_message_iter_open_container(&objs, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH, &kStation);
        plain_dict(&entry, station, "connected");
        dbus_message_iter_close_container(&objs, &entry);
    } else {
        const char* text = "no interfaces";

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{os}", &objs);
        dbus_message_iter_open_container(&objs, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH, &kStation);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &text);
        dbus_message_iter_close_container(&objs, &entry);
    }

    dbus_message_iter_close_container(&it, &objs);
}

static void managed(DBusMessage* call) {
    round_no++;
    printf("managed %d\n", round_no);

    if (round_no == 1) {
        reply_error(call);
    } else if (round_no >= 11) {
        dbus_message_ref(call);
        puts("managed held");

        return;
    } else {
        DBusMessage* reply = dbus_message_new_method_return(call);

        if (round_no == 2) {
            /* a change lands while this refresh is still in flight */
            emit_changed(kStation);
            usleep(300 * 1000);
            tree(reply, "connecting", 0);
        } else if (round_no == 3) {
            tree(reply, "roaming", 1);
        } else if (round_no <= 8) {
            mistyped(reply, round_no - 4);
        } else {
            tree(reply, "disconnected", 0);
        }

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    }

    /* the mistyped rounds follow one another on their own */
    if (round_no >= 4 && round_no <= 8) {
        dbus_connection_flush(conn);
        emit_changed(kStation);
    }
}

static void ordered(DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, arr, e;

    printf("ordered %d\n", round_no);
    dbus_message_iter_init_append(reply, &it);

    if (round_no == 3) {
        const char* paths[] = {kNet1, kNet2, "/dev0/ghost"};
        int16_t strengths[] = {-1000, -10000, -5000};

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(on)", &arr);

        for (int i = 0; i < 3; i++) {
            dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &e);
            dbus_message_iter_append_basic(&e, DBUS_TYPE_OBJECT_PATH, &paths[i]);
            dbus_message_iter_append_basic(&e, DBUS_TYPE_INT16, &strengths[i]);
            dbus_message_iter_close_container(&arr, &e);
        }
    } else if (round_no == 2) {
        dbus_message_unref(reply);
        reply_error(call);

        return;
    } else if (round_no == 9) {
        const char* weak = "weak";

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(os)", &arr);
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_OBJECT_PATH, &kNet1);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &weak);
        dbus_message_iter_close_container(&arr, &e);
    } else {
        int32_t path = 0x7ffffff0;
        int16_t strength = -5000;

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(in)", &arr);
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_INT32, &path);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_INT16, &strength);
        dbus_message_iter_close_container(&arr, &e);
    }

    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);

    /* the list with string strengths leads straight into the one with
     * integer paths */
    if (round_no == 9) {
        dbus_connection_flush(conn);
        emit_changed(kStation);
    }
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c;
    (void)data;

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects")) {
        managed(msg);
    } else if (dbus_message_is_method_call(msg, "net.connman.iwd.Station", "GetOrderedNetworks")) {
        ordered(msg);
    } else if (dbus_message_is_method_call(msg, "net.connman.iwd.AgentManager", "RegisterAgent")) {
        const char* path = "";

        dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
        snprintf(agent_path, sizeof(agent_path), "%s", path);
        snprintf(agent_dest, sizeof(agent_dest), "%s", dbus_message_get_sender(msg));

        DBusMessage* reply = dbus_message_new_method_return(msg);

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        puts("agent registered");
    } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        reply_error(msg);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_connection_flush(conn);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void pump(int ms) {
    for (int i = 0; i < ms / 10; i++) {
        dbus_connection_read_write_dispatch(conn, 10);
    }
}

static void wait_go(const char* name) {
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

static DBusMessage* agent_call(const char* method) {
    DBusMessage* call = dbus_message_new_method_call(agent_dest, agent_path, "net.connman.iwd.Agent", method);

    if (!strcmp(method, "RequestPassphrase")) {
        dbus_message_append_args(call, DBUS_TYPE_OBJECT_PATH, &kNet1, DBUS_TYPE_INVALID);
    }

    return call;
}

/* a blocking agent call that must be answered, error or not */
static void agent_ask(const char* method) {
    DBusError err;
    DBusMessage* call = agent_call(method);

    dbus_error_init(&err);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 2000, &err);

    dbus_message_unref(call);

    if (!reply) {
        printf("agent %s: %s\n", method, err.name ? err.name : "?");

        if (err.name && !strcmp(err.name, DBUS_ERROR_NO_REPLY)) {
            fprintf(stderr, "the agent left %s unanswered\n", method);
            exit(1);
        }

        dbus_error_free(&err);
    } else {
        printf("agent %s answered\n", method);
        dbus_message_unref(reply);
    }
}

static DBusPendingCall* agent_request(void) {
    DBusMessage* call = agent_call("RequestPassphrase");
    DBusPendingCall* pending = NULL;

    dbus_connection_send_with_reply(conn, call, &pending, 30000);
    dbus_message_unref(call);
    dbus_connection_flush(conn);

    return pending;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(120);

    conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, NULL);

    if (!conn) {
        return 1;
    }

    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    if (dbus_bus_request_name(conn, "net.connman.iwd", DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) !=
        DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        return 2;
    }

    DBusObjectPathVTable vt = {0};

    vt.message_function = message;
    dbus_connection_register_fallback(conn, "/", &vt, NULL);
    puts("iwd ready");

    wait_go("tree");
    emit_changed(kStation);
    wait_go("mistyped");
    emit_changed(kStation);
    wait_go("agent");

    if (!agent_path[0]) {
        fprintf(stderr, "no agent registered\n");
        return 1;
    }

    /* the same passphrase asked twice: the first request must not be left
     * hanging once the second replaces it */
    DBusPendingCall* first = agent_request();
    DBusPendingCall* second = agent_request();

    for (int i = 0; i < 150 && !dbus_pending_call_get_completed(first); i++) {
        pump(10);
    }

    if (!dbus_pending_call_get_completed(first)) {
        fprintf(stderr, "the replaced passphrase request was left hanging\n");
        return 1;
    }

    puts("replaced request answered");
    wait_go("asked");

    /* iwd gives up: the prompt goes, then the agent is released */
    agent_ask("Cancel");
    dbus_pending_call_cancel(second);
    agent_ask("Release");
    agent_ask("Bogus");

    /* a signal on the agent's own path */
    {
        DBusMessage* sig = dbus_message_new_signal(agent_path, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded");

        dbus_connection_send(conn, sig, NULL);
        dbus_message_unref(sig);
        dbus_connection_flush(conn);
    }

    wait_go("cancelled");

    /* left pending into the shutdown: a prompt and an object read */
    agent_request();
    wait_go("pending");
    puts("iwd edges done");

    for (;;) {
        pump(100);
    }
}
