/* Drives imway's org.freedesktop.Notifications end to end: server info,
 * capabilities, two posted toasts (one critical), CloseNotification and the
 * NotificationClosed signal with the by-request reason. */
#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* conn;

static uint32_t notify(const char* summary, const char* body, int critical, int32_t expire_ms) {
    DBusMessage* call = dbus_message_new_method_call(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify");
    DBusMessageIter it, arr;
    const char* app = "notif-test";
    uint32_t replaces = 0;
    const char* icon = "";

    dbus_message_iter_init_append(call, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &app);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &replaces);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &icon);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &summary);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &body);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
    if (critical) {
        DBusMessageIter entry, var;
        const char* key = "urgency";
        unsigned char urgency = 2;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "y", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_BYTE, &urgency);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&arr, &entry);
    }
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &expire_ms);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 3000, NULL);
    dbus_message_unref(call);
    if (!reply) return 0;

    uint32_t id = 0;
    dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    return id;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(45);
    conn = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);
    if (!conn) return 1;
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    int owned = 0;
    for (int i = 0; i < 100; i++) {
        if (dbus_bus_name_has_owner(conn, "org.freedesktop.Notifications", NULL)) {
            owned = 1;
            break;
        }
        usleep(100000);
    }
    if (!owned) {
        fprintf(stderr, "no notifications server on the bus\n");
        return 1;
    }

    DBusMessage* call = dbus_message_new_method_call(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "GetServerInformation");
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 3000, NULL);
    dbus_message_unref(call);
    if (!reply) return 1;
    const char *name = "", *vendor = "", *version = "", *spec = "";
    dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &vendor,
                          DBUS_TYPE_STRING, &version, DBUS_TYPE_STRING, &spec, DBUS_TYPE_INVALID);
    if (strcmp(name, "imway") != 0) {
        fprintf(stderr, "unexpected server: %s\n", name);
        return 1;
    }
    dbus_message_unref(reply);
    puts("server imway");

    call = dbus_message_new_method_call(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "GetCapabilities");
    reply = dbus_connection_send_with_reply_and_block(conn, call, 3000, NULL);
    dbus_message_unref(call);
    if (!reply) return 1;
    char** caps = NULL;
    int ncaps = 0;
    int has_body = 0;
    if (dbus_message_get_args(reply, NULL, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                              &caps, &ncaps, DBUS_TYPE_INVALID)) {
        for (int i = 0; i < ncaps; i++) {
            if (!strcmp(caps[i], "body")) has_body = 1;
        }
        dbus_free_string_array(caps);
    }
    dbus_message_unref(reply);
    if (!has_body) {
        fprintf(stderr, "capabilities lack body\n");
        return 1;
    }
    puts("caps ok");

    dbus_bus_add_match(conn,
        "type='signal',interface='org.freedesktop.Notifications',member='NotificationClosed'", NULL);
    dbus_connection_flush(conn);

    uint32_t one = notify("toast-one", "the first toast body", 0, 60000);
    if (!one) {
        fprintf(stderr, "Notify one failed\n");
        return 1;
    }
    printf("posted one %u\n", one);

    sleep(2);

    uint32_t two = notify("toast-two", "a critical toast", 1, 60000);
    if (!two || two == one) {
        fprintf(stderr, "Notify two failed (%u vs %u)\n", two, one);
        return 1;
    }
    printf("posted two %u\n", two);

    call = dbus_message_new_method_call(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "CloseNotification");
    dbus_message_append_args(call, DBUS_TYPE_UINT32, &two, DBUS_TYPE_INVALID);
    reply = dbus_connection_send_with_reply_and_block(conn, call, 3000, NULL);
    dbus_message_unref(call);
    if (!reply) return 1;
    dbus_message_unref(reply);

    /* reason 3: closed by a CloseNotification call */
    for (;;) {
        if (!dbus_connection_read_write_dispatch(conn, 500)) return 1;
        DBusMessage* msg;
        while ((msg = dbus_connection_pop_message(conn))) {
            if (dbus_message_is_signal(msg, "org.freedesktop.Notifications", "NotificationClosed")) {
                uint32_t id = 0, reason = 0;
                dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &id,
                                      DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID);
                dbus_message_unref(msg);
                if (id == two) {
                    if (reason != 3) {
                        fprintf(stderr, "wrong close reason %u\n", reason);
                        return 1;
                    }
                    printf("closed two reason %u\n", reason);
                    goto done;
                }
                continue;
            }
            dbus_message_unref(msg);
        }
    }

done:
    /* stay on the bus while the scenario inspects the history panel */
    for (;;) {
        pause();
    }
}
