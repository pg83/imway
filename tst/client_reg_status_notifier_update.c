/* A tray item that repaints its pixmap magenta -> cyan after two seconds and
 * signals NewIcon: the dock must re-fetch and show the new color. */
#include "sni_item.inc"

#include <time.h>

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)c; (void)data;
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        send_properties(msg);
        dbus_connection_flush(conn);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    DBusObjectPathVTable vt = {0};
    vt.message_function = message;

    int rc = sni_register("org.example.ImwaySniUpdateTest", &vt);
    if (rc) return rc;

    time_t start = time(NULL);
    int repainted = 0;

    while (dbus_connection_read_write_dispatch(conn, 200)) {
        if (!repainted && time(NULL) - start >= 2) {
            sni_r = 0;
            sni_g = 255;
            sni_b = 255;
            sni_new_icon();
            repainted = 1;
            puts("repainted");
        }
    }
    return 0;
}
