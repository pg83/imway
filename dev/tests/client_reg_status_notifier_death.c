/* A tray item that registers and then just serves properties: the scenario
 * kills it and the dock must drop the icon when the bus name vanishes. */
#include "sni_item.inc"

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

    int rc = sni_register("org.example.ImwaySniDeathTest", &vt);
    if (rc) return rc;

    while (dbus_connection_read_write_dispatch(conn, 1000)) {
    }
    return 0;
}
