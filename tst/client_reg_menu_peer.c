/* A hostile DBusMenu peer and appmenu registrar caller. One window points
 * its appmenu at this service, which answers GetLayout with every shape a
 * broken or malicious application can produce: a wrong first argument, no
 * layout struct, a root without an id, items with mistyped properties, a
 * tree too deep and a tree too big. The scenario reads the model back from
 * the compositor's dump between the stages, which the client and the
 * scenario hand over through go-files in XDG_RUNTIME_DIR. After that the
 * endpoint changes under the compositor (an invalid address, a unique
 * name, a call left pending, the name changing owner), and the registrar
 * is asked malformed and unknown questions it must answer promptly. */

#include "wl_util.h"

#include <appmenu-client-protocol.h>

#include <dbus/dbus.h>

static DBusConnection* bus;
static const char* kName = "org.example.ImwayMenuPeer";
static const char* kIface = "com.canonical.dbusmenu";
static const char* kRegistrar = "com.canonical.AppMenu.Registrar";
static const char* kRegistrarPath = "/com/canonical/AppMenu/Registrar";

enum mode {
    MODE_BAD_FIRST,
    MODE_NO_STRUCT,
    MODE_BAD_ROOT,
    MODE_EMPTY,
    MODE_TEXT_ROOT,
    MODE_RICH,
    MODE_TOO_MANY,
    MODE_SMALL,
    MODE_HOLD,
};

static enum mode mode = MODE_BAD_FIRST;
static uint32_t small_revision = 11;
static int layouts;
static DBusMessage* held;
static struct org_kde_kwin_appmenu* appmenu;
static struct org_kde_kwin_appmenu_manager* appmenu_manager;

static void pump(int ms) {
    for (int i = 0; i < ms / 10; i++) {
        wl_display_flush(wl_dpy);
        wl_display_dispatch_pending(wl_dpy);
        dbus_connection_read_write_dispatch(bus, 10);
    }
}

/* the scenario asserts the dump, then lets the client go on */
static void stage(const char* name) {
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

static void prop_int(DBusMessageIter* dict, const char* key, int32_t value) {
    DBusMessageIter entry, var;

    open_var(dict, key, "i", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &value);
    close_var(dict, &entry, &var);
}

static void prop_u32(DBusMessageIter* dict, const char* key, uint32_t value) {
    DBusMessageIter entry, var;

    open_var(dict, key, "u", &entry, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &value);
    close_var(dict, &entry, &var);
}

static void prop_strings(DBusMessageIter* dict, const char* key, const char* a, const char* b) {
    DBusMessageIter entry, var, arr;

    open_var(dict, key, "as", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &a);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &b);
    dbus_message_iter_close_container(&var, &arr);
    close_var(dict, &entry, &var);
}

static void prop_bytes(DBusMessageIter* dict, const char* key, const unsigned char* bytes, int count) {
    DBusMessageIter entry, var, arr;

    open_var(dict, key, "ay", &entry, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &arr);

    if (count) {
        dbus_message_iter_append_fixed_array(&arr, DBUS_TYPE_BYTE, &bytes, count);
    }

    dbus_message_iter_close_container(&var, &arr);
    close_var(dict, &entry, &var);
}

/* a (ia{sv}av) node inside a variant of the parent's children array */
static void node_open(DBusMessageIter* children, int32_t id, DBusMessageIter* var, DBusMessageIter* node,
                      DBusMessageIter* props) {
    dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "(ia{sv}av)", var);
    dbus_message_iter_open_container(var, DBUS_TYPE_STRUCT, NULL, node);
    dbus_message_iter_append_basic(node, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(node, DBUS_TYPE_ARRAY, "{sv}", props);
}

static void node_children(DBusMessageIter* node, DBusMessageIter* props, DBusMessageIter* kids) {
    dbus_message_iter_close_container(node, props);
    dbus_message_iter_open_container(node, DBUS_TYPE_ARRAY, "v", kids);
}

static void node_close(DBusMessageIter* children, DBusMessageIter* var, DBusMessageIter* node,
                       DBusMessageIter* kids) {
    dbus_message_iter_close_container(node, kids);
    dbus_message_iter_close_container(var, node);
    dbus_message_iter_close_container(children, var);
}

static void leaf(DBusMessageIter* children, int32_t id, const char* label) {
    DBusMessageIter var, node, props, kids;

    node_open(children, id, &var, &node, &props);

    if (label) {
        prop_string(&props, "label", label);
    }

    node_children(&node, &props, &kids);
    node_close(children, &var, &node, &kids);
}

/* a chain of nested items, one per level, deeper than the model keeps */
static void chain(DBusMessageIter* children, int32_t id, int left) {
    DBusMessageIter var, node, props, kids;

    node_open(children, id, &var, &node, &props);

    if (id == 100) {
        prop_string(&props, "disposition", "normal");
        prop_string(&props, "x-imway-unknown", "ignored");
    }

    node_children(&node, &props, &kids);

    if (left > 0) {
        chain(&kids, id + 1, left - 1);
    }

    node_close(children, &var, &node, &kids);
}

static uint32_t crc32(const unsigned char* p, size_t n) {
    uint32_t c = 0xffffffffu;

    for (size_t i = 0; i < n; i++) {
        c ^= p[i];

        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
        }
    }

    return ~c;
}

static void put32(unsigned char* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

/* a well-formed PNG header announcing w x h, then an empty IDAT and IEND:
 * enough for the header to be read, too big for a menu icon */
static int png_header(unsigned char* out, uint32_t w, uint32_t h) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    int at = 0;

    memcpy(out, sig, 8);
    at = 8;
    put32(out + at, 13);
    memcpy(out + at + 4, "IHDR", 4);
    put32(out + at + 8, w);
    put32(out + at + 12, h);
    out[at + 16] = 8;
    out[at + 17] = 6;
    out[at + 18] = 0;
    out[at + 19] = 0;
    out[at + 20] = 0;
    put32(out + at + 21, crc32(out + at + 4, 17));
    at += 25;

    static const char* tail[] = {"IDAT", "IEND"};

    for (int i = 0; i < 2; i++) {
        put32(out + at, 0);
        memcpy(out + at + 4, tail[i], 4);
        put32(out + at + 8, crc32(out + at + 4, 4));
        at += 12;
    }

    return at;
}

static void rich_children(DBusMessageIter* children) {
    static const unsigned char junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    DBusMessageIter var, node, props, kids, entry, v;

    /* item 1: mistyped values fall back, an unknown toggle and a label with
     * both kinds of underscore */
    node_open(children, 1, &var, &node, &props);
    prop_string(&props, "label", "Save__As_x_");
    prop_string(&props, "toggle-type", "radio");
    prop_string(&props, "toggle-state", "on");
    prop_string(&props, "enabled", "no");
    prop_string(&props, "disposition", "informative");
    prop_int(&props, "type", 5);
    prop_string(&props, "shortcut", "Ctrl+S");
    prop_string(&props, "icon-data", "not bytes");
    prop_string(&props, "", "no key");
    node_children(&node, &props, &kids);
    {
        /* item 2, a child: the parent becomes a submenu without saying so */
        DBusMessageIter var2, node2, props2, kids2;

        node_open(&kids, 2, &var2, &node2, &props2);
        prop_string(&props2, "disposition", "warning");
        prop_string(&props2, "toggle-type", "bogus");
        prop_strings(&props2, "shortcut", "Control", "S");
        prop_strings(&props2, "icon-data", "a", "b");
        prop_bytes(&props2, "icon-data", NULL, 0);
        prop_bytes(&props2, "icon-data", junk, (int)sizeof(junk));

        unsigned char wide[64], tall[64];

        prop_bytes(&props2, "icon-data", wide, png_header(wide, 2000, 1));
        prop_bytes(&props2, "icon-data", tall, png_header(tall, 1, 2000));
        prop_u32(&props2, "label", 7);

        /* past the icon size the model takes */
        static unsigned char huge[4 * 1024 * 1024 + 1];

        prop_bytes(&props2, "icon-data", huge, (int)sizeof(huge));
        node_children(&node2, &props2, &kids2);
        node_close(&kids, &var2, &node2, &kids2);
    }
    node_close(children, &var, &node, &kids);

    /* item 3: properties that are not variants */
    {
        DBusMessageIter pv, pn, pp, pk, pe;
        int32_t id = 3;
        const char* key = "label";
        const char* value = "plain";

        dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "(ia{ss}av)", &pv);
        dbus_message_iter_open_container(&pv, DBUS_TYPE_STRUCT, NULL, &pn);
        dbus_message_iter_append_basic(&pn, DBUS_TYPE_INT32, &id);
        dbus_message_iter_open_container(&pn, DBUS_TYPE_ARRAY, "{ss}", &pp);
        dbus_message_iter_open_container(&pp, DBUS_TYPE_DICT_ENTRY, NULL, &pe);
        dbus_message_iter_append_basic(&pe, DBUS_TYPE_STRING, &key);
        dbus_message_iter_append_basic(&pe, DBUS_TYPE_STRING, &value);
        dbus_message_iter_close_container(&pp, &pe);
        dbus_message_iter_close_container(&pn, &pp);
        dbus_message_iter_open_container(&pn, DBUS_TYPE_ARRAY, "v", &pk);
        dbus_message_iter_close_container(&pn, &pk);
        dbus_message_iter_close_container(&pv, &pn);
        dbus_message_iter_close_container(children, &pv);
    }

    /* item 4: an id and nothing else */
    {
        DBusMessageIter pv, pn;
        int32_t id = 4;

        dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "(i)", &pv);
        dbus_message_iter_open_container(&pv, DBUS_TYPE_STRUCT, NULL, &pn);
        dbus_message_iter_append_basic(&pn, DBUS_TYPE_INT32, &id);
        dbus_message_iter_close_container(&pv, &pn);
        dbus_message_iter_close_container(children, &pv);
    }

    /* not a node at all, and a node whose id is a string */
    {
        const char* junk_text = "junk";
        const char* bad_id = "five";

        dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "s", &v);
        dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &junk_text);
        dbus_message_iter_close_container(children, &v);

        dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "(s)", &v);
        dbus_message_iter_open_container(&v, DBUS_TYPE_STRUCT, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &bad_id);
        dbus_message_iter_close_container(&v, &entry);
        dbus_message_iter_close_container(children, &v);
    }

    /* 100..119: twenty levels, the model stops at its depth limit */
    chain(children, 100, 19);
}

static void send_layout(DBusConnection* c, DBusMessage* call) {
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it, root, props, children;
    uint32_t revision = 0;
    int32_t root_id = 0;

    dbus_message_iter_init_append(reply, &it);

    switch (mode) {
        case MODE_BAD_FIRST: {
            const char* text = "not a revision";

            dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &text);
            break;
        }
        case MODE_NO_STRUCT:
            revision = 3;
            dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &revision);
            break;
        case MODE_EMPTY:
            break;
        case MODE_TEXT_ROOT: {
            const char* text = "root";

            revision = 6;
            dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &revision);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &text);
            break;
        }
        case MODE_BAD_ROOT: {
            const char* text = "root";

            revision = 5;
            dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &revision);
            dbus_message_iter_open_container(&it, DBUS_TYPE_STRUCT, NULL, &root);
            dbus_message_iter_append_basic(&root, DBUS_TYPE_STRING, &text);
            dbus_message_iter_close_container(&it, &root);
            break;
        }
        default:
            revision = mode == MODE_RICH ? 7 : mode == MODE_TOO_MANY ? 8 : small_revision;
            dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &revision);
            dbus_message_iter_open_container(&it, DBUS_TYPE_STRUCT, NULL, &root);
            dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &root_id);
            dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}", &props);
            /* the root's own properties have no item to land on */
            prop_string(&props, "label", "root");
            dbus_message_iter_close_container(&root, &props);
            dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "v", &children);

            if (mode == MODE_RICH) {
                rich_children(&children);
            } else if (mode == MODE_TOO_MANY) {
                for (int32_t i = 0; i < 4100; i++) {
                    leaf(&children, 1000 + i, NULL);
                }
            } else {
                leaf(&children, 1, "small");
            }

            dbus_message_iter_close_container(&root, &children);
            dbus_message_iter_close_container(&it, &root);
            break;
    }

    dbus_connection_send(c, reply, NULL);
    dbus_message_unref(reply);
}

static DBusHandlerResult message(DBusConnection* c, DBusMessage* msg, void* data) {
    (void)data;

    if (!dbus_message_is_method_call(msg, kIface, "GetLayout")) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    layouts++;
    printf("getlayout %s %d\n", dbus_message_get_path(msg), layouts);

    if (mode == MODE_HOLD) {
        held = dbus_message_ref(msg);
    } else {
        send_layout(c, msg);
    }

    dbus_connection_flush(c);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void await_layouts(int count) {
    for (int i = 0; i < 500 && layouts < count; i++) {
        pump(20);
    }

    if (layouts < count) {
        fprintf(stderr, "the compositor asked for %d layouts, not %d\n", layouts, count);
        exit(1);
    }
}

static DBusMessage* signal_on(const char* path, const char* member) {
    return dbus_message_new_signal(path, kIface, member);
}

static void emit(DBusMessage* sig) {
    dbus_connection_send(bus, sig, NULL);
    dbus_message_unref(sig);
    dbus_connection_flush(bus);
}

/* a revision that is no number reads as 0, which always refreshes */
static void layout_updated_text(const char* path) {
    DBusMessage* sig = signal_on(path, "LayoutUpdated");
    const char* revision = "seven";

    dbus_message_append_args(sig, DBUS_TYPE_STRING, &revision, DBUS_TYPE_INVALID);
    emit(sig);
}

static void layout_updated(const char* path, int with_revision, uint32_t revision) {
    DBusMessage* sig = signal_on(path, "LayoutUpdated");
    int32_t parent = 0;

    if (with_revision) {
        dbus_message_append_args(sig, DBUS_TYPE_UINT32, &revision, DBUS_TYPE_INT32, &parent, DBUS_TYPE_INVALID);
    }

    emit(sig);
}

/* ItemActivationRequested doubles as the barrier: the dump shows its id
 * once everything this peer sent before it has been read */
static void activation(const char* path, int32_t id) {
    DBusMessage* sig = signal_on(path, "ItemActivationRequested");
    uint32_t stamp = 0;

    dbus_message_append_args(sig, DBUS_TYPE_INT32, &id, DBUS_TYPE_UINT32, &stamp, DBUS_TYPE_INVALID);
    emit(sig);
}

static void activation_malformed(const char* path) {
    DBusMessage* sig = signal_on(path, "ItemActivationRequested");
    const char* text = "seven";

    emit(signal_on(path, "ItemActivationRequested"));
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
    emit(sig);
}

/* a (i...) row with the given tail; kind 0: id only, 1: an extra int */
static void short_row(DBusMessageIter* arr, int32_t id, int kind) {
    DBusMessageIter row;
    int32_t extra = 5;

    dbus_message_iter_open_container(arr, DBUS_TYPE_STRUCT, NULL, &row);
    dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &id);

    if (kind) {
        dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &extra);
    }

    dbus_message_iter_close_container(arr, &row);
}

static void properties_updates(const char* path) {
    DBusMessage* sig;
    DBusMessageIter it, arr, row, props, names;
    int32_t id;
    const char* label = "label";

    /* no arguments at all, and a signal the menu does not know */
    emit(signal_on(path, "ItemsPropertiesUpdated"));
    emit(signal_on(path, "Poke"));

    /* rows whose properties are a string, then a removed list that is no
     * array */
    {
        const char* text = "label";

        sig = signal_on(path, "ItemsPropertiesUpdated");
        dbus_message_iter_init_append(sig, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(is)", &arr);
        id = 1;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &row);
        dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &id);
        dbus_message_iter_append_basic(&row, DBUS_TYPE_STRING, &text);
        dbus_message_iter_close_container(&arr, &row);
        dbus_message_iter_close_container(&it, &arr);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &text);
        emit(sig);
    }

    /* the first argument is not the updated-items array */
    sig = signal_on(path, "ItemsPropertiesUpdated");
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &label, DBUS_TYPE_INVALID);
    emit(sig);

    /* updates for an unknown item, a row without properties, a real one;
     * no removed-properties argument at all */
    sig = signal_on(path, "ItemsPropertiesUpdated");
    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
    id = 99;
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &row);
    dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&row, DBUS_TYPE_ARRAY, "{sv}", &props);
    prop_string(&props, "label", "ghost");
    dbus_message_iter_close_container(&row, &props);
    dbus_message_iter_close_container(&arr, &row);
    id = 4;
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &row);
    dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&row, DBUS_TYPE_ARRAY, "{sv}", &props);
    prop_string(&props, "label", "Four");
    prop_string(&props, "toggle-type", "checkmark");
    dbus_message_iter_close_container(&row, &props);
    dbus_message_iter_close_container(&arr, &row);
    dbus_message_iter_close_container(&it, &arr);
    emit(sig);

    sig = signal_on(path, "ItemsPropertiesUpdated");
    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(i)", &arr);
    short_row(&arr, 1, 0);
    dbus_message_iter_close_container(&it, &arr);
    emit(sig);

    /* removals: an unknown item, rows without or with a mistyped name
     * list, and item 1's label */
    sig = signal_on(path, "ItemsPropertiesUpdated");
    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ias)", &arr);
    id = 99;
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &row);
    dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&row, DBUS_TYPE_ARRAY, "s", &names);
    dbus_message_iter_append_basic(&names, DBUS_TYPE_STRING, &label);
    dbus_message_iter_close_container(&row, &names);
    dbus_message_iter_close_container(&arr, &row);
    id = 1;
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &row);
    dbus_message_iter_append_basic(&row, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&row, DBUS_TYPE_ARRAY, "s", &names);
    dbus_message_iter_append_basic(&names, DBUS_TYPE_STRING, &label);
    dbus_message_iter_close_container(&row, &names);
    dbus_message_iter_close_container(&arr, &row);
    dbus_message_iter_close_container(&it, &arr);
    emit(sig);

    sig = signal_on(path, "ItemsPropertiesUpdated");
    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(i)", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ii)", &arr);
    short_row(&arr, 1, 1);
    dbus_message_iter_close_container(&it, &arr);
    emit(sig);

    sig = signal_on(path, "ItemsPropertiesUpdated");
    dbus_message_iter_init_append(sig, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(i)", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(i)", &arr);
    short_row(&arr, 2, 0);
    dbus_message_iter_close_container(&it, &arr);
    emit(sig);
}

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
    (void)d;

    if (!strcmp(iface, org_kde_kwin_appmenu_manager_interface.name)) {
        appmenu_manager = wl_registry_bind(r, name, &org_kde_kwin_appmenu_manager_interface, ver < 2 ? ver : 2);
    }
}

static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}

static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void set_address(const char* service, const char* path) {
    org_kde_kwin_appmenu_set_address(appmenu, service, path);
    wl_display_roundtrip(wl_dpy);
}

/* a registrar method call; the answer must come back well inside the
 * timeout, an error is fine but silence is not */
static DBusMessage* registrar(DBusConnection* c, const char* method, int argc, uint32_t window, const char* path) {
    DBusMessage* call = dbus_message_new_method_call(kRegistrar, kRegistrarPath, kRegistrar, method);
    DBusError err;

    if (argc >= 1) {
        dbus_message_append_args(call, DBUS_TYPE_UINT32, &window, DBUS_TYPE_INVALID);
    }

    if (argc >= 2) {
        dbus_message_append_args(call, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
    }

    dbus_error_init(&err);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(c, call, 2000, &err);

    dbus_message_unref(call);

    if (!reply) {
        printf("registrar %s: %s\n", method, err.name ? err.name : "?");

        if (err.name && !strcmp(err.name, DBUS_ERROR_NO_REPLY)) {
            fprintf(stderr, "the registrar left %s unanswered\n", method);
            exit(1);
        }

        dbus_error_free(&err);
    }

    return reply;
}

static int listed(DBusConnection* c, uint32_t window) {
    DBusMessage* reply = registrar(c, "GetMenus", 0, 0, NULL);
    DBusMessageIter it, arr;
    int found = 0;

    if (!reply || !dbus_message_iter_init(reply, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) {
        fprintf(stderr, "GetMenus returned no list\n");
        exit(1);
    }

    dbus_message_iter_recurse(&it, &arr);

    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
        DBusMessageIter row;
        uint32_t id = 0;

        dbus_message_iter_recurse(&arr, &row);
        dbus_message_iter_get_basic(&row, &id);

        if (id == window) {
            found++;
        }

        dbus_message_iter_next(&arr);
    }

    dbus_message_unref(reply);

    return found;
}

static DBusConnection* second_connection(void) {
    DBusConnection* c = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!c) {
        fprintf(stderr, "no second bus connection\n");
        exit(2);
    }

    dbus_connection_set_exit_on_disconnect(c, FALSE);

    return c;
}

/* a peer impersonating the bus: a unicast NameOwnerChanged claiming the
 * victim left. Only org.freedesktop.DBus speaks for bus names. */
static void forge_departure(const char* service, const char* victim) {
    DBusMessage* call = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");
    const char* owner = "";
    const char* none = "";

    dbus_message_append_args(call, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, call, 2000, NULL);

    dbus_message_unref(call);

    if (!reply || !dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "nobody owns %s\n", service);
        exit(1);
    }

    DBusMessage* sig = dbus_message_new_signal(DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "NameOwnerChanged");

    dbus_message_set_destination(sig, owner);
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &victim, DBUS_TYPE_STRING, &victim, DBUS_TYPE_STRING, &none, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    emit(sig);

    /* and one whose arguments are not even names */
    sig = dbus_message_new_signal(DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "NameOwnerChanged");
    dbus_message_set_destination(sig, owner);
    emit(sig);
}

static void registrar_checks(void) {
    DBusMessage* reply;

    /* malformed and unknown calls are answered with errors */
    reply = registrar(bus, "RegisterWindow", 0, 0, NULL);

    if (reply) {
        fprintf(stderr, "RegisterWindow without arguments succeeded\n");
        exit(1);
    }

    registrar(bus, "UnregisterWindow", 0, 0, NULL);
    registrar(bus, "GetMenuForWindow", 0, 0, NULL);
    registrar(bus, "Bogus", 0, 0, NULL);
    puts("registrar answered malformed calls");

    /* registering the same window twice keeps one row */
    for (int i = 0; i < 2; i++) {
        reply = registrar(bus, "RegisterWindow", 2, 4242, "/Menu");

        if (!reply) {
            fprintf(stderr, "RegisterWindow failed\n");
            exit(1);
        }

        dbus_message_unref(reply);
    }

    if (listed(bus, 4242) != 1) {
        fprintf(stderr, "a re-registered window is listed %d times\n", listed(bus, 4242));
        exit(1);
    }

    /* unregistering one window leaves the others */
    reply = registrar(bus, "RegisterWindow", 2, 4343, "/Menu");

    if (reply) {
        dbus_message_unref(reply);
    }

    reply = registrar(bus, "UnregisterWindow", 1, 4343, NULL);

    if (reply) {
        dbus_message_unref(reply);
    }

    if (listed(bus, 4343) || listed(bus, 4242) != 1) {
        fprintf(stderr, "unregistering one window touched another\n");
        exit(1);
    }

    /* a stranger cannot unregister it; its own window dies with it */
    DBusConnection* other = second_connection();

    reply = registrar(other, "UnregisterWindow", 1, 4242, NULL);

    if (reply) {
        dbus_message_unref(reply);
    }

    reply = registrar(other, "RegisterWindow", 2, 5151, "/Other");

    if (reply) {
        dbus_message_unref(reply);
    }

    if (listed(bus, 4242) != 1 || listed(bus, 5151) != 1) {
        fprintf(stderr, "a stranger's unregister took the window away\n");
        exit(1);
    }

    dbus_connection_close(other);
    dbus_connection_unref(other);

    for (int i = 0; i < 100 && listed(bus, 5151); i++) {
        pump(50);
    }

    if (listed(bus, 5151) || listed(bus, 4242) != 1) {
        fprintf(stderr, "the departed peer's window was not dropped alone\n");
        exit(1);
    }

    puts("registrar tracked its peers");

    forge_departure(kRegistrar, dbus_bus_get_unique_name(bus));

    if (listed(bus, 4242) != 1) {
        fprintf(stderr, "a forged NameOwnerChanged dropped the registration\n");
        exit(1);
    }

    puts("registrar ignored the impostor");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(100);

    bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);

    if (!bus) {
        fprintf(stderr, "no session bus\n");
        return 2;
    }

    dbus_connection_set_exit_on_disconnect(bus, FALSE);

    if (dbus_bus_request_name(bus, kName, DBUS_NAME_FLAG_ALLOW_REPLACEMENT | DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) !=
        DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "cannot own a bus name\n");
        return 2;
    }

    DBusObjectPathVTable vt = {0};

    vt.message_function = message;
    dbus_connection_register_fallback(bus, "/", &vt, NULL);

    /* the compositor hears of the name's new owner on the bus and of the
     * menu address on the wayland socket, in no fixed order; an owner
     * change after the menu attached would ask for one more layout than
     * the stages count. A registrar roundtrip queues behind the bus's
     * NameOwnerChanged, so once it is answered the compositor has seen it. */
    listed(bus, 0);

    if (wl_boot()) {
        return 2;
    }

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!appmenu_manager) {
        fprintf(stderr, "no appmenu manager\n");
        return 2;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "menu-peer", 300, 200, 0xFF405060);
    appmenu = org_kde_kwin_appmenu_manager_create(appmenu_manager, top.surface);

    /* the layout replies the model cannot use; a property update before
     * any layout has nothing to land on */
    set_address(kName, "/Menu");
    await_layouts(1);
    properties_updates("/Menu");
    mode = MODE_NO_STRUCT;
    layout_updated("/Menu", 1, 0);
    await_layouts(2);
    activation("/Menu", 1);
    stage("unusable");

    mode = MODE_BAD_ROOT;
    layout_updated("/Menu", 1, 0);
    await_layouts(3);

    /* a reply with nothing in it, then a revision and text for a root */
    mode = MODE_EMPTY;
    layout_updated("/Menu", 1, 0);
    await_layouts(4);
    mode = MODE_TEXT_ROOT;
    layout_updated_text("/Menu");
    await_layouts(5);
    activation("/Menu", 2);
    stage("rootless");

    mode = MODE_RICH;
    layout_updated("/Menu", 1, 0);
    await_layouts(6);
    activation("/Menu", 3);
    stage("rich");

    /* too big to take: the rich model stays */
    mode = MODE_TOO_MANY;
    layout_updated("/Menu", 1, 8);
    await_layouts(7);
    activation("/Menu", 4);
    stage("too-many");

    /* stale, argument-less and older revisions refresh nothing; malformed
     * activation requests are dropped */
    layout_updated("/Menu", 1, 7);
    layout_updated("/Menu", 0, 0);
    layout_updated("/Menu", 1, 3);
    activation_malformed("/Menu");
    properties_updates("/Menu");
    activation("/Menu", 5);
    stage("updates");

    /* an address that is no address */
    set_address(kName, "not a path");
    set_address("not..a..name", "/Menu");
    stage("invalid");

    /* the endpoint by its unique name; a layout left unanswered while the
     * window moves on to another object */
    const char* unique = dbus_bus_get_unique_name(bus);

    mode = MODE_HOLD;
    set_address(unique, "/Menu2");
    await_layouts(8);
    mode = MODE_SMALL;
    set_address(unique, "/Menu3");
    await_layouts(9);
    small_revision = 10;
    send_layout(bus, held);
    dbus_message_unref(held);
    held = NULL;
    activation("/Menu3", 6);
    stage("moved");

    /* the well-known name changes hands: the new owner serves the layout,
     * and when it leaves the model goes with it */
    small_revision = 21;
    set_address(kName, "/Menu");
    await_layouts(10);

    DBusConnection* heir = second_connection();

    dbus_connection_register_fallback(heir, "/", &vt, NULL);

    if (dbus_bus_request_name(heir, kName, DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) !=
        DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "the heir could not take the name\n");
        return 1;
    }

    small_revision = 22;

    for (int i = 0; i < 500 && layouts < 11; i++) {
        dbus_connection_read_write_dispatch(heir, 10);
        pump(10);
    }

    if (layouts < 11) {
        fprintf(stderr, "the new owner was never asked for the layout\n");
        return 1;
    }

    dbus_connection_flush(heir);
    stage("heir");

    dbus_connection_close(heir);
    dbus_connection_unref(heir);
    stage("orphaned");

    registrar_checks();
    puts("menu peer done");

    /* the registration stays live into the compositor's shutdown */
    for (;;) {
        pump(100);
    }
}
