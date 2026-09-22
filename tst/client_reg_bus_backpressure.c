/* A busy session-bus peer, for a compositor whose bus connection has a
 * tiny send buffer and holds at most 4 KiB of undispatched messages (the chaos
 * words in the scenario header). Three hundred appmenu registrations with
 * long paths, then GetMenus: the reply is far larger than the send buffer,
 * so the compositor writes it in parts as the socket drains. Then a burst
 * of a hundred Notify calls in flight at once: the compositor stops
 * reading after each one and starts again once it is dispatched. Every
 * call must be answered. */
#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* kRegistrar = "com.canonical.AppMenu.Registrar";
static const char* kRegistrarPath = "/com/canonical/AppMenu/Registrar";

enum { kWindows = 300, kNotes = 100 };

static DBusMessage* notify_call(int i) {
    DBusMessage* call = dbus_message_new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                                     "org.freedesktop.Notifications", "Notify");
    DBusMessageIter it, arr;
    const char* app = "backpressure";
    const char* icon = "";
    const char* body = "";
    char summary[64];
    const char* s = summary;
    uint32_t replaces = 0;
    int32_t expire = -1;

    snprintf(summary, sizeof(summary), "burst %d", i);
    dbus_message_iter_init_append(call, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &app);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &replaces);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &icon);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &s);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &body);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &expire);

    return call;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    DBusConnection* bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!bus) {
        return 2;
    }

    for (uint32_t window = 1; window <= kWindows; window++) {
        char path[160];
        const char* p = path;

        snprintf(path, sizeof(path), "/org/example/imway/backpressure/a/rather/long/object/path/for/window/%u/menu", window);

        DBusMessage* call = dbus_message_new_method_call(kRegistrar, kRegistrarPath, kRegistrar, "RegisterWindow");

        dbus_message_append_args(call, DBUS_TYPE_UINT32, &window, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
        /* the bus caps the replies one connection may owe another */
        dbus_message_set_no_reply(call, TRUE);
        dbus_connection_send(bus, call, NULL);
        dbus_message_unref(call);
    }

    DBusMessage* call = dbus_message_new_method_call(kRegistrar, kRegistrarPath, kRegistrar, "GetMenus");
    DBusError err;

    dbus_error_init(&err);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, call, 20000, &err);

    dbus_message_unref(call);

    if (!reply) {
        fprintf(stderr, "GetMenus: %s\n", err.name ? err.name : "?");
        return 1;
    }

    DBusMessageIter it, arr;
    int rows = 0;

    if (dbus_message_iter_init(reply, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&it, &arr);

        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
            rows++;
            dbus_message_iter_next(&arr);
        }
    }

    char* wire = NULL;
    int bytes = 0;

    if (dbus_message_marshal(reply, &wire, &bytes)) {
        dbus_free(wire);
    }

    printf("menus %d bytes %d\n", rows, bytes);
    dbus_message_unref(reply);

    if (rows != kWindows) {
        fprintf(stderr, "GetMenus listed %d of %d windows\n", rows, kWindows);
        return 1;
    }

    DBusPendingCall* pending[kNotes];

    for (int i = 0; i < kNotes; i++) {
        DBusMessage* note = notify_call(i);

        pending[i] = NULL;
        dbus_connection_send_with_reply(bus, note, &pending[i], 20000);
        dbus_message_unref(note);
    }

    int answered = 0;

    for (int i = 0; i < kNotes; i++) {
        if (!pending[i]) {
            continue;
        }

        dbus_pending_call_block(pending[i]);

        DBusMessage* r = dbus_pending_call_steal_reply(pending[i]);
        uint32_t id = 0;

        if (r && dbus_message_get_args(r, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID) && id) {
            answered++;
        }

        if (r) {
            dbus_message_unref(r);
        }

        dbus_pending_call_unref(pending[i]);
    }

    printf("notes %d\n", answered);

    if (answered != kNotes) {
        fprintf(stderr, "%d of %d notifications were answered\n", answered, kNotes);
        return 1;
    }

    puts("backpressure done");

    return 0;
}
