/* Windows whose app_ids resolve through every odd corner of the icon store
 * (one per argument, mapped side by side), and notifications whose icon
 * strings take the store's string-only paths: an absolute png and svg, a
 * path of another format, and a mixed-case name. Prints each notification
 * id with the icon it asked for. */
#include "wl_util.h"

#include <dbus/dbus.h>

static uint32_t notify(DBusConnection* bus, const char* icon) {
    DBusMessage* call = dbus_message_new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                                     "org.freedesktop.Notifications", "Notify");
    DBusMessageIter it, arr;
    const char* app = "icon-edges";
    const char* summary = "icon edge";
    const char* body = "";
    uint32_t replaces = 0;
    int32_t expire = 0;

    dbus_message_iter_init_append(call, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &app);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &replaces);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &icon);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &summary);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &body);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &expire);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, call, 3000, NULL);
    uint32_t id = 0;

    dbus_message_unref(call);

    if (reply) {
        dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
        dbus_message_unref(reply);
    }

    return id;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) {
        return 1;
    }

    static struct wl_toplevel_ctx tops[24];

    for (int i = 1; i < argc && i < 24; i++) {
        wl_make_toplevel(&tops[i], argv[i], 120, 80, 0xFF203040 + (uint32_t)i * 0x100010);
    }

    wl_display_roundtrip(wl_dpy);
    puts("windows mapped");

    DBusConnection* bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!bus) {
        return 2;
    }

    const char* dir = getenv("XDG_RUNTIME_DIR");
    char png[512], svg[512], other[512];

    snprintf(png, sizeof(png), "%s/abs.png", dir);
    snprintf(svg, sizeof(svg), "%s/abs.svg", dir);
    snprintf(other, sizeof(other), "%s/abs.xpm", dir);

    const char* icons[] = {"no-such-icon", png, svg, other, "Imway-Small"};

    for (size_t i = 0; i < sizeof(icons) / sizeof(icons[0]); i++) {
        printf("notification %u %s\n", notify(bus, icons[i]), i == 1 ? "abs-png" : i == 2 ? "abs-svg" : i == 3 ? "abs-other" : i == 4 ? "mixed-case" : "none");
    }

    puts("notifications posted");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
