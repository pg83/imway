/* Malformed callers of org.freedesktop.Notifications. Every Notify whose
 * fixed arguments are missing or mistyped must be refused with an error
 * rather than left unanswered; hints of the wrong shape (integer keys, a
 * string urgency, an urgency that is no variant) and a missing timeout
 * must not keep a well-formed notification from posting; a bare
 * CloseNotification and an unknown method get answers too. */
#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* conn;

enum shape {
    SHAPE_EMPTY,
    SHAPE_APP,
    SHAPE_REPLACES,
    SHAPE_ICON,
    SHAPE_SUMMARY,
    SHAPE_BODY,
    SHAPE_INT_KEYS,
    SHAPE_STRING_HINTS,
    SHAPE_STRING_URGENCY,
    SHAPE_NO_TIMEOUT,
};

static DBusMessage* call(const char* method) {
    return dbus_message_new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                        "org.freedesktop.Notifications", method);
}

/* the well-formed prefix up to the argument `shape` breaks */
static void append(DBusMessage* msg, enum shape shape) {
    DBusMessageIter it, arr, entry, var;
    const char* text = "malformed";
    uint32_t replaces = 0;
    int32_t number = 7;

    dbus_message_iter_init_append(msg, &it);

    if (shape == SHAPE_EMPTY) {
        return;
    }

    if (shape == SHAPE_APP) {
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &number);
        return;
    }

    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &text);

    if (shape == SHAPE_REPLACES) {
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &text);
        return;
    }

    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &replaces);

    for (int field = SHAPE_ICON; field <= SHAPE_BODY; field++) {
        if (shape == field) {
            dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &number);
            return;
        }

        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &text);
    }

    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_close_container(&it, &arr);

    if (shape == SHAPE_INT_KEYS) {
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{iv}", &arr);
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        number = 0x7ffffff0;
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_INT32, &number);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &text);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&arr, &entry);
        dbus_message_iter_close_container(&it, &arr);
    } else if (shape == SHAPE_STRING_HINTS) {
        const char* key = "urgency";
        const char* value = "2";

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{ss}", &arr);
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &value);
        dbus_message_iter_close_container(&arr, &entry);
        dbus_message_iter_close_container(&it, &arr);
    } else {
        const char* key = "urgency";
        const char* value = "critical";

        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&arr, &entry);
        dbus_message_iter_close_container(&it, &arr);
    }

    if (shape != SHAPE_NO_TIMEOUT) {
        number = -1;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &number);
    }
}

/* the reply, or NULL for an error reply; silence fails the client */
static DBusMessage* ask(DBusMessage* msg, const char* what) {
    DBusError err;

    dbus_error_init(&err);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);

    dbus_message_unref(msg);

    if (!reply) {
        printf("%s: %s\n", what, err.name ? err.name : "?");

        if (err.name && !strcmp(err.name, DBUS_ERROR_NO_REPLY)) {
            fprintf(stderr, "%s was left unanswered\n", what);
            exit(1);
        }

        dbus_error_free(&err);
    }

    return reply;
}

int main(void) {
    static const char* broken[] = {"no arguments", "an integer app name", "a string replaces id", "an integer icon",
                                   "an integer summary", "an integer body"};

    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(45);

    conn = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!conn) {
        return 2;
    }

    for (int shape = SHAPE_EMPTY; shape <= SHAPE_BODY; shape++) {
        DBusMessage* msg = call("Notify");

        append(msg, (enum shape)shape);

        DBusMessage* reply = ask(msg, broken[shape]);

        if (reply) {
            fprintf(stderr, "a Notify with %s was accepted\n", broken[shape]);
            return 1;
        }
    }

    puts("malformed notifications refused");

    for (int shape = SHAPE_INT_KEYS; shape <= SHAPE_NO_TIMEOUT; shape++) {
        DBusMessage* msg = call("Notify");

        append(msg, (enum shape)shape);

        DBusMessage* reply = ask(msg, "odd hints");
        uint32_t id = 0;

        if (!reply || !dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID) || !id) {
            fprintf(stderr, "a notification with odd hints was not posted\n");
            return 1;
        }

        dbus_message_unref(reply);
        printf("posted %u\n", id);
    }

    puts("odd hints posted");

    DBusMessage* reply = ask(call("CloseNotification"), "a bare CloseNotification");

    if (!reply) {
        fprintf(stderr, "a bare CloseNotification failed\n");
        return 1;
    }

    dbus_message_unref(reply);

    if (ask(call("Bogus"), "an unknown method")) {
        fprintf(stderr, "an unknown method was answered as if valid\n");
        return 1;
    }

    puts("notifications malformed done");

    return 0;
}
