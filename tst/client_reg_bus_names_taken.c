/* Another desktop's session services already on the bus: this process owns
 * the appmenu registrar, the StatusNotifierWatcher and the notification
 * service before the compositor starts (it runs from imway-pre), and
 * holds them until the scenario ends. */
#include <dbus/dbus.h>

#include <stdio.h>
#include <unistd.h>

int main(void) {
    static const char* names[] = {
        "com.canonical.AppMenu.Registrar",
        "org.kde.StatusNotifierWatcher",
        "org.freedesktop.Notifications",
    };

    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(90);

    DBusConnection* bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!bus) {
        fprintf(stderr, "no session bus\n");
        return 2;
    }

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (dbus_bus_request_name(bus, names[i], DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
            fprintf(stderr, "cannot own %s\n", names[i]);
            return 2;
        }
    }

    puts("names held");

    while (dbus_connection_read_write_dispatch(bus, 1000)) {
    }

    return 0;
}
