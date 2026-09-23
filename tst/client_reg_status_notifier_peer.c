/* A hostile StatusNotifierItem peer. It asks the watcher malformed and
 * unknown questions, serves GetAll with mistyped values and pixmaps every
 * way broken, sends PropertiesChanged in shapes the spec does not allow,
 * registers the same item twice, one by object path and one on behalf of
 * another connection, and lets a third connection die with a property
 * read still pending. The scenario reads the tray model back from the
 * compositor's dump between stages handed over through go-files. */

#include <dbus/dbus.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection* bus;
static const char* kName = "org.example.ImwaySniPeer";
static const char* kWatcher = "org.kde.StatusNotifierWatcher";
static const char* kWatcherPath = "/StatusNotifierWatcher";
static const char* kItem = "org.kde.StatusNotifierItem";
static const char* kProps = "org.freedesktop.DBus.Properties";

enum mode {
    MODE_FULL,
    MODE_EMPTY,
    MODE_NOT_A_DICT,
    MODE_PROXIED,
    MODE_SILENT,
};

static enum mode mode = MODE_FULL;
static int getalls;

static void pump_one(DBusConnection* c, int ms) {
    for (int i = 0; i < ms / 10; i++) {
        dbus_connection_read_write_dispatch(c, 10);
    }
}

static void stage(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);
    printf("stage %s\n", name);

    for (int i = 0; i < 3000; i++) {
        if (access(path, F_OK) == 0) {
            return;
        }

        pump_one(bus, 20);
    }

    fprintf(stderr, "the scenario never released stage %s\n", name);
    exit(1);
}

static void open_var(DBusMessageIter* dict, const char* key, const char* sig, DBusMessageIter* entry,
                     DBusMessageIter* var) {
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, entry);
    dbus_message_iter_append_basic(entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(entry, DBUS_TYPE_VARIANT, sig, var);
}

static void close_var(DBusMessageIter* dict, DBusMessageIter* entry, DBusMessageIter* var) {
    dbus_message_iter_close_container(entry, var);
    dbus_message_iter_close_container(dict, entry);
}

static void prop_string(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;

    open_var(dict, key, "s", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    close_var(dict, &entry, &var);
}

static void prop_path(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry, var;

    open_var(dict, key, "o", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &value);
    close_var(dict, &entry, &var);
}

static void prop_int(DBusMessageIter* dict, const char* key, int32_t value) {
    DBusMessageIter entry, var;

    open_var(dict, key, "i", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &value);
    close_var(dict, &entry, &var);
}

/* one (iiay) image of a solid ARGB color, with `bytes` of pixel data */
static void image(DBusMessageIter* images, int32_t w, int32_t h, int bytes, uint32_t argb) {
    DBusMessageIter st, arr;
    unsigned char data[4096];

    for (int i = 0; i + 3 < bytes && i + 3 < (int)sizeof(data); i += 4) {
        data[i] = argb >> 24;
        data[i + 1] = argb >> 16;
        data[i + 2] = argb >> 8;
        data[i + 3] = argb;
    }

    const unsigned char* p = data;

    dbus_message_iter_open_container(images, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &w);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &h);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "y", &arr);

    if (bytes) {
        dbus_message_iter_append_fixed_array(&arr, DBUS_TYPE_BYTE, &p, bytes);
    }

    dbus_message_iter_close_container(&st, &arr);
    dbus_message_iter_close_container(images, &st);
}

static void open_pixmap(DBusMessageIter* dict, const char* key, DBusMessageIter* entry, DBusMessageIter* var,
                        DBusMessageIter* images) {
    open_var(dict, key, "a(iiay)", entry, var);
    dbus_message_iter_open_container(var, DBUS_TYPE_ARRAY, "(iiay)", images);
}

static void close_pixmap(DBusMessageIter* dict, DBusMessageIter* entry, DBusMessageIter* var,
                         DBusMessageIter* images) {
    dbus_message_iter_close_container(var, images);
    close_var(dict, entry, var);
}

static void full_properties(DBusMessageIter* dict) {
    DBusMessageIter entry, var, images, st;
    int32_t four = 4;
    const char* wide = "wide";

    prop_string(dict, "Id", "peer-one");
    prop_int(dict, "Title", 5);
    prop_string(dict, "Title", "Hostile peer");
    prop_string(dict, "DesktopEntry", "imway-peer.desktop");
    prop_string(dict, "Status", "NeedsAttention");
    prop_string(dict, "IconName", "peer-icon");
    prop_string(dict, "AttentionIconName", "peer-alert");
    prop_string(dict, "ItemIsMenu", "yes");
    prop_string(dict, "Menu", "");
    prop_string(dict, "IconPixmap", "not a pixmap");

    /* a valid pixmap first, so the broken one after it has one to drop */
    open_pixmap(dict, "IconPixmap", &entry, &var, &images);
    image(&images, 2, 2, 16, 0xff00ff00);
    close_pixmap(dict, &entry, &var, &images);

    /* images without pixel data, and with a width that is not a number */
    open_var(dict, "IconPixmap", "a(ii)", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "(ii)", &images);
    dbus_message_iter_open_container(&images, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &four);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &four);
    dbus_message_iter_close_container(&images, &st);
    dbus_message_iter_close_container(&var, &images);
    close_var(dict, &entry, &var);

    open_var(dict, "IconPixmap", "a(si)", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "(si)", &images);
    dbus_message_iter_open_container(&images, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &wide);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &four);
    dbus_message_iter_close_container(&images, &st);
    dbus_message_iter_close_container(&var, &images);
    close_var(dict, &entry, &var);

    /* the one that counts: every broken size, then the largest good one */
    open_pixmap(dict, "IconPixmap", &entry, &var, &images);
    image(&images, 0, 16, 0, 0);
    image(&images, 16, 0, 0, 0);
    image(&images, 2000, 1, 0, 0);
    image(&images, 1, 2000, 0, 0);
    image(&images, 16, 16, 40, 0xffff00ff);
    image(&images, 16, 16, 1100, 0xffff00ff); /* more bytes than 16x16 needs */
    image(&images, 8, 8, 256, 0xff0000ff);
    close_pixmap(dict, &entry, &var, &images);

    open_pixmap(dict, "AttentionIconPixmap", &entry, &var, &images);
    image(&images, 2, 2, 16, 0xffffff00);
    close_pixmap(dict, &entry, &var, &images);
}

static void reply_getall(DBusConnection* c, DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, dict;

    dbus_message_iter_init_append(reply, &it);

    if (mode == MODE_FULL || mode == MODE_PROXIED) {
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);

        if (mode == MODE_FULL) {
            full_properties(&dict);
        } else {
            prop_string(&dict, "Id", "proxied");
        }

        dbus_message_iter_close_container(&it, &dict);
    } else if (mode == MODE_NOT_A_DICT) {
        const char* text = "not a dict";

        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &text);
    }

    dbus_connection_send(c, reply, NULL);
    dbus_message_unref(reply);
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)data;

    if (dbus_message_is_method_call(msg, kProps, "GetAll")) {
        getalls++;
        printf("getall %s %d\n", dbus_message_get_path(msg), getalls);

        if (!strcmp(dbus_message_get_path(msg), "/Unreadable")) {
            DBusMessage* err = dbus_message_new_error(msg, DBUS_ERROR_ACCESS_DENIED, "not telling");

            dbus_connection_send(c, err, NULL);
            dbus_message_unref(err);
        } else if (mode != MODE_SILENT) {
            reply_getall(c, msg);
        }
    } else if (dbus_message_is_method_call(msg, kItem, "ContextMenu")) {
        puts("context menu asked");
    } else if (dbus_message_is_method_call(msg, "com.canonical.dbusmenu", "AboutToShow")) {
        /* an answer without the needs-update flag */
        DBusMessage* reply = dbus_message_new_method_return(msg);

        dbus_connection_send(c, reply, NULL);
        dbus_message_unref(reply);
        puts("about to show asked");
    } else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        DBusMessage* err = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "not faked");

        dbus_connection_send(c, err, NULL);
        dbus_message_unref(err);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_connection_flush(c);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusConnection* connection(void) {
    DBusConnection* c = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!c) {
        fprintf(stderr, "no session bus\n");
        exit(2);
    }

    dbus_connection_set_exit_on_disconnect(c, FALSE);

    DBusObjectPathVTable vt = {0};

    vt.message_function = message;
    dbus_connection_register_fallback(c, "/", &vt, NULL);

    return c;
}

/* a watcher call; errors are answers, silence is the failure */
static DBusMessage* watcher(DBusConnection* c, const char* iface, const char* method, const char* a,
                            const char* b) {
    DBusMessage* call = dbus_message_new_method_call(kWatcher, kWatcherPath, iface, method);
    DBusError err;

    if (a) {
        dbus_message_append_args(call, DBUS_TYPE_STRING, &a, DBUS_TYPE_INVALID);
    }

    if (b) {
        dbus_message_append_args(call, DBUS_TYPE_STRING, &b, DBUS_TYPE_INVALID);
    }

    dbus_error_init(&err);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(c, call, 2000, &err);

    dbus_message_unref(call);

    if (!reply) {
        printf("watcher %s: %s\n", method, err.name ? err.name : "?");

        if (err.name && !strcmp(err.name, DBUS_ERROR_NO_REPLY)) {
            fprintf(stderr, "the watcher left %s unanswered\n", method);
            exit(1);
        }

        dbus_error_free(&err);
    }

    return reply;
}

static void expect_error(DBusMessage* reply, const char* what) {
    if (reply) {
        fprintf(stderr, "%s was answered as if valid\n", what);
        exit(1);
    }
}

static void register_item(DBusConnection* c, const char* what) {
    DBusMessage* reply = watcher(c, kWatcher, "RegisterStatusNotifierItem", what, NULL);

    if (!reply) {
        fprintf(stderr, "registering %s failed\n", what);
        exit(1);
    }

    dbus_message_unref(reply);
}

static void emit(DBusConnection* c, DBusMessage* sig) {
    dbus_connection_send(c, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(c);
}

static DBusMessage* changed(void) {
    return dbus_message_new_signal("/StatusNotifierItem", kProps, "PropertiesChanged");
}

static void malformed_changes(void) {
    DBusMessage* sig;
    DBusMessageIter it, dict, entry, inval;
    int32_t number = 3;
    const char* key = "Title";
    const char* value = "Not a variant";
    const char* other = "org.example.Other";

    /* no arguments at all, then an invalidated list that is text */
    emit(bus, changed());

    sig = changed();
    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &kItem);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dbus_message_iter_close_container(&it, &dict);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &value);
    emit(bus, sig);

    sig = changed();
    dbus_message_append_args(sig, DBUS_TYPE_INT32, &number, DBUS_TYPE_INVALID);
    emit(bus, sig);

    sig = changed();
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &other, DBUS_TYPE_INVALID);
    emit(bus, sig);

    sig = changed();
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &kItem, DBUS_TYPE_INVALID);
    emit(bus, sig);

    /* a changed dict of plain strings, and none of the invalidated list */
    sig = changed();
    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &kItem);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{ss}", &dict);
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&it, &dict);
    emit(bus, sig);

    /* integer keys, then an empty invalidated list */
    sig = changed();
    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &kItem);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{iv}", &dict);
    {
        DBusMessageIter var;

        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_INT32, &number);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&dict, &entry);
    }
    dbus_message_iter_close_container(&it, &dict);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);
    dbus_message_iter_close_container(&it, &inval);
    emit(bus, sig);

    /* not a dict at all where the changed properties belong */
    sig = changed();
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &kItem, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
    emit(bus, sig);

    /* a signal of some other interface on the menu's path */
    emit(bus, dbus_message_new_signal("/Menu", other, "Poke"));
}

/* a real change: a new title, a menu, and an invalidated pixmap that the
 * compositor re-reads with GetAll */
static void menu_change(const char* menu) {
    DBusMessage* sig = changed();
    DBusMessageIter it, dict, inval;
    const char* stale = "IconPixmap";

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &kItem);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    prop_string(&dict, "Title", "Renamed");

    if (menu[0]) {
        prop_path(&dict, "Menu", menu);
    } else {
        prop_string(&dict, "Menu", "");
    }

    dbus_message_iter_close_container(&it, &dict);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);

    if (menu[0]) {
        dbus_message_iter_append_basic(&inval, DBUS_TYPE_STRING, &stale);
    }

    dbus_message_iter_close_container(&it, &inval);
    emit(bus, sig);
}

static void drop_pixmap(void) {
    DBusMessage* sig = changed();
    DBusMessageIter it, dict, entry, var, images, inval;

    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &kItem);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    open_pixmap(&dict, "IconPixmap", &entry, &var, &images);
    close_pixmap(&dict, &entry, &var, &images);
    dbus_message_iter_close_container(&it, &dict);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);
    dbus_message_iter_close_container(&it, &inval);
    emit(bus, sig);
}

static void await_getalls(int count) {
    for (int i = 0; i < 500 && getalls < count; i++) {
        pump_one(bus, 20);
    }

    if (getalls < count) {
        fprintf(stderr, "the compositor read the properties %d times, not %d\n", getalls, count);
        exit(1);
    }
}

static void forge_departure(const char* victim) {
    DBusMessage* call = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");
    const char* owner = "";
    const char* none = "";

    dbus_message_append_args(call, DBUS_TYPE_STRING, &kWatcher, DBUS_TYPE_INVALID);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, call, 2000, NULL);

    dbus_message_unref(call);

    if (!reply || !dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "nobody owns the watcher\n");
        exit(1);
    }

    DBusMessage* sig = dbus_message_new_signal(DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "NameOwnerChanged");

    dbus_message_set_destination(sig, owner);
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &victim, DBUS_TYPE_STRING, &victim, DBUS_TYPE_STRING, &none,
                             DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    emit(bus, sig);
}

/* the watcher's own list of what is registered */
static int still_registered(const char* entry) {
    DBusMessage* reply = watcher(bus, kProps, "Get", kWatcher, "RegisteredStatusNotifierItems");
    DBusMessageIter it, var, arr;
    int found = 0;

    if (!reply || !dbus_message_iter_init(reply, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_VARIANT) {
        fprintf(stderr, "the watcher did not list its items\n");
        exit(1);
    }

    dbus_message_iter_recurse(&it, &var);
    dbus_message_iter_recurse(&var, &arr);

    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
        const char* value = "";

        dbus_message_iter_get_basic(&arr, &value);
        found = found || !strcmp(value, entry);
        dbus_message_iter_next(&arr);
    }

    dbus_message_unref(reply);

    return found;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(100);

    bus = connection();

    if (dbus_bus_request_name(bus, kName, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "cannot own a bus name\n");
        return 2;
    }

    /* malformed and unknown questions get errors, never silence */
    expect_error(watcher(bus, kWatcher, "RegisterStatusNotifierItem", NULL, NULL), "a registration without a name");
    expect_error(watcher(bus, kWatcher, "RegisterStatusNotifierItem", "", NULL), "a registration of an empty name");
    expect_error(watcher(bus, kProps, "Get", kWatcher, "Bogus"), "an unknown watcher property");
    expect_error(watcher(bus, kProps, "Get", "org.example.Other", "ProtocolVersion"), "a property of another interface");
    expect_error(watcher(bus, kProps, "Get", NULL, NULL), "a property read without arguments");
    expect_error(watcher(bus, kWatcher, "Bogus", NULL, NULL), "an unknown watcher method");
    puts("watcher answered malformed calls");

    register_item(bus, kName);
    await_getalls(1);
    stage("full");

    malformed_changes();
    register_item(bus, kName);
    await_getalls(2);
    stage("malformed");

    /* a menu appears and the pixmap is invalidated: the re-read gets an
     * empty answer, then a NewStatus one that is no dict */
    mode = MODE_EMPTY;
    menu_change("/Menu");
    await_getalls(3);
    mode = MODE_NOT_A_DICT;
    emit(bus, dbus_message_new_signal("/StatusNotifierItem", kItem, "NewStatus"));
    await_getalls(4);
    register_item(bus, "/StatusNotifierItem");
    stage("menu");

    /* the menu goes, and the pixmap with it */
    menu_change("");
    drop_pixmap();
    register_item(bus, kName);
    stage("dropped");

    /* an item registered on behalf of another connection answers to it */
    DBusConnection* proxied = connection();
    const char* proxied_name = dbus_bus_get_unique_name(proxied);
    int before = getalls;

    mode = MODE_PROXIED;
    register_item(bus, proxied_name);

    for (int i = 0; i < 500 && getalls < before + 1; i++) {
        pump_one(proxied, 10);
        pump_one(bus, 10);
    }

    emit(proxied, dbus_message_new_signal("/StatusNotifierItem", kItem, "NewTitle"));

    for (int i = 0; i < 500 && getalls < before + 2; i++) {
        pump_one(proxied, 10);
        pump_one(bus, 10);
    }

    if (getalls < before + 2) {
        fprintf(stderr, "the proxied item was not re-read on its own signal\n");
        return 1;
    }

    puts("proxied item followed its connection");

    /* a peer that dies with its property read unanswered */
    DBusConnection* silent = connection();

    mode = MODE_SILENT;
    before = getalls;
    register_item(silent, "/StatusNotifierItem");

    for (int i = 0; i < 500 && getalls < before + 1; i++) {
        pump_one(silent, 10);
    }

    dbus_connection_close(silent);
    dbus_connection_unref(silent);
    mode = MODE_FULL;
    stage("silent");

    /* an impostor of the bus claims this peer left: its items stay */
    forge_departure(dbus_bus_get_unique_name(bus));

    if (!still_registered("org.example.ImwaySniPeer/StatusNotifierItem")) {
        fprintf(stderr, "a forged NameOwnerChanged dropped the items\n");
        return 1;
    }

    puts("impostor ignored");
    stage("forged");

    /* an item of the same connection at another path, whose properties
     * cannot be read */
    register_item(bus, "/Unreadable");
    stage("unreadable");

    puts("status notifier peer done");

    for (;;) {
        pump_one(bus, 100);
        pump_one(proxied, 100);
    }
}
