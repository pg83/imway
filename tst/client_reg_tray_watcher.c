/* The watcher side of the tray protocol. A registered item asks the
 * compositor's org.kde.StatusNotifierWatcher for its properties one at a
 * time and all at once, registers a host, and then announces a change with
 * PropertiesChanged — including an invalidated array, which makes the
 * compositor re-read the item. */

#include "sni_item.inc"

static int activations;

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c; (void)data;

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        send_properties(msg);
        printf("served properties\n");
    } else if (dbus_message_is_method_call(msg, "org.kde.StatusNotifierItem", "Activate")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        activations++;
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

static DBusMessage* watcher_call(const char* method, const char* iface, const char* prop) {
    DBusMessage* call = dbus_message_new_method_call(
        "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        iface ? "org.freedesktop.DBus.Properties" : "org.kde.StatusNotifierWatcher", method);

    if (iface) {
        dbus_message_append_args(call, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
    }

    if (prop) {
        dbus_message_append_args(call, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
    }

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 3000, NULL);

    dbus_message_unref(call);

    return reply;
}

static int read_int_variant(DBusMessage* reply, int32_t* out) {
    DBusMessageIter it, var;

    if (!reply || !dbus_message_iter_init(reply, &it)) return 0;
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_VARIANT) return 0;
    dbus_message_iter_recurse(&it, &var);
    if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT32) return 0;
    dbus_message_iter_get_basic(&var, out);

    return 1;
}

static int read_bool_variant(DBusMessage* reply, dbus_bool_t* out) {
    DBusMessageIter it, var;

    if (!reply || !dbus_message_iter_init(reply, &it)) return 0;
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_VARIANT) return 0;
    dbus_message_iter_recurse(&it, &var);
    if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN) return 0;
    dbus_message_iter_get_basic(&var, out);

    return 1;
}

/* the registered-items array: every entry is "service" + "/path" */
static int count_items(DBusMessage* reply) {
    DBusMessageIter it, var, arr;
    int n = 0;

    if (!reply || !dbus_message_iter_init(reply, &it)) return -1;
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_VARIANT) return -1;
    dbus_message_iter_recurse(&it, &var);
    if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_ARRAY) return -1;
    dbus_message_iter_recurse(&var, &arr);

    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
        const char* s = "";

        dbus_message_iter_get_basic(&arr, &s);
        printf("registered item %s\n", s);
        n++;
        dbus_message_iter_next(&arr);
    }

    return n;
}

static int count_all(DBusMessage* reply) {
    DBusMessageIter it, dict;
    int n = 0;

    if (!reply || !dbus_message_iter_init(reply, &it)) return -1;
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) return -1;
    dbus_message_iter_recurse(&it, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        const char* key = "";

        dbus_message_iter_recurse(&dict, &entry);
        dbus_message_iter_get_basic(&entry, &key);
        printf("watcher property %s\n", key);
        n++;
        dbus_message_iter_next(&dict);
    }

    return n;
}

static void announce_change(void) {
    DBusMessage* sig = dbus_message_new_signal(
        "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    DBusMessageIter it, dict, invalidated;
    const char* iface = "org.kde.StatusNotifierItem";
    const char* stale = "IconPixmap";

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dict_string(&dict, "Title", "renamed by properties changed");
    dict_string(&dict, "Status", "NeedsAttention");
    dbus_message_iter_close_container(&it, &dict);
    /* a non-empty invalidated array makes the compositor re-read everything */
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &invalidated);
    dbus_message_iter_append_basic(&invalidated, DBUS_TYPE_STRING, &stale);
    dbus_message_iter_close_container(&it, &invalidated);
    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    DBusObjectPathVTable vt = {0};

    vt.message_function = message;

    int rc = sni_register("org.example.ImwayTrayWatcher", &vt);

    if (rc) {
        fprintf(stderr, "registration failed: %d\n", rc);
        return rc;
    }

    /* the watcher answers a host registration too */
    DBusMessage* host = watcher_call("RegisterStatusNotifierHost", NULL, NULL);

    if (!host) {
        fprintf(stderr, "no reply to RegisterStatusNotifierHost\n");
        return 1;
    }

    dbus_message_unref(host);
    puts("host registered");

    int32_t version = -1;
    DBusMessage* reply = watcher_call("Get", "org.kde.StatusNotifierWatcher", "ProtocolVersion");

    if (!read_int_variant(reply, &version)) {
        fprintf(stderr, "ProtocolVersion is not an int32\n");
        return 1;
    }

    dbus_message_unref(reply);
    printf("protocol version %d\n", version);

    dbus_bool_t hosted = FALSE;

    reply = watcher_call("Get", "org.kde.StatusNotifierWatcher", "IsStatusNotifierHostRegistered");

    if (!read_bool_variant(reply, &hosted) || !hosted) {
        fprintf(stderr, "the watcher does not claim a host\n");
        return 1;
    }

    dbus_message_unref(reply);
    puts("host flag set");

    reply = watcher_call("Get", "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems");

    int items = count_items(reply);

    if (reply) dbus_message_unref(reply);

    if (items < 1) {
        fprintf(stderr, "the watcher lists %d items\n", items);
        return 1;
    }

    /* an unknown property and a foreign interface are simply not answered */
    reply = watcher_call("Get", "org.kde.StatusNotifierWatcher", "NoSuchProperty");

    if (reply) {
        dbus_message_unref(reply);
        fprintf(stderr, "an unknown property was answered\n");
        return 1;
    }

    reply = watcher_call("Get", "org.example.NotTheWatcher", "ProtocolVersion");

    if (reply) {
        dbus_message_unref(reply);
        fprintf(stderr, "a foreign interface was answered\n");
        return 1;
    }

    reply = watcher_call("GetAll", "org.kde.StatusNotifierWatcher", NULL);

    int all = count_all(reply);

    if (reply) dbus_message_unref(reply);

    if (all < 3) {
        fprintf(stderr, "GetAll returned %d properties\n", all);
        return 1;
    }

    announce_change();

    /* the compositor re-reads the item after the invalidated array; keep
     * serving until it has, then report */
    for (int i = 0; i < 200; i++) {
        dbus_connection_read_write_dispatch(conn, 50);
    }

    printf("tray watcher done (activations %d)\n", activations);

    return 0;
}
