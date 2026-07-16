#include "composer.h"
#include "wifi.h"
#include "wifi_nm.h"
#include "dbus_conn.h"
#include "scene.h"
#include "util.h"

#include <dbus/dbus.h>

#include <std/ios/sys.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr const char* kNm = "org.freedesktop.NetworkManager";
    constexpr const char* kNmPath = "/org/freedesktop/NetworkManager";
    constexpr const char* kProps = "org.freedesktop.DBus.Properties";
    constexpr const char* kDev = "org.freedesktop.NetworkManager.Device";
    constexpr const char* kWireless = "org.freedesktop.NetworkManager.Device.Wireless";
    constexpr const char* kAp = "org.freedesktop.NetworkManager.AccessPoint";

    constexpr u32 kDeviceWifi = 2;
    constexpr u32 kStateActivated = 100;
    constexpr u32 kStateConfig = 40;
    constexpr u32 kStateNeedAuth = 60;

    struct NmWifi;

    DBusHandlerResult onSignal(DBusConnection*, DBusMessage* msg, void* data);

    struct Known {
        StringBuilder ssid;
        StringBuilder path; // the Connection object path
    };

    struct NmWifi: public Wifi {
        Composer* c = nullptr;
        DBusConnection* conn = nullptr;

        StringBuilder devicePath;
        WifiState st = WifiState::unavailable;

        ObjList<WifiNetwork> netAlloc;
        Vector<WifiNetwork*> nets;

        ObjList<Known> knownAlloc;
        Vector<Known*> known;

        // NM has no agent prompt: we gather the secret up front and pass it
        // to AddAndActivateConnection
        bool wantPass = false;
        StringBuilder passAp;
        StringBuilder passSsid;

        NmWifi(Composer& comp, DBusConnection* c);

        WifiState state() override;
        void networksImpl(VisitorFace&& vis) override;
        void scan() override;
        void connect(StringView path) override;
        void disconnect() override;
        void forget(StringView path) override;
        bool passphraseWanted() override;
        StringView passphraseFor() override;
        void providePassphrase(StringView pw) override;
        void cancelPassphrase() override;

        void refresh();
        void notify();
        WifiNetwork* byPath(StringView path);
        Known* knownForSsid(StringView ssid);

        // blocking property reads — refresh is rare (a signal or menu open),
        // and linear blocking calls are far easier to get right than an
        // async fan-out for something no one can test here
        DBusMessage* propGet(StringView path, const char* iface, const char* name);
        u32 propU32(StringView path, const char* iface, const char* name);
        void variantSsid(DBusMessageIter* var, StringBuilder& out);

        bool findWifiDevice();
        void loadKnown();
        void addAccessPoint(StringView apPath, StringView activePath);

        void activate(StringView apPath);
        void addAndActivate(StringView apPath, StringView ssid, StringView psk);
    };

    StringView iterStr(DBusMessageIter* v) {
        int t = dbus_message_iter_get_arg_type(v);

        if (t != DBUS_TYPE_STRING && t != DBUS_TYPE_OBJECT_PATH) {
            return {};
        }

        const char* s = "";

        dbus_message_iter_get_basic(v, &s);

        return StringView(s);
    }
}

NmWifi::NmWifi(Composer& comp, DBusConnection* c)
    : c(&comp)
    , conn(c)
    , netAlloc(comp.pool)
    , knownAlloc(comp.pool)
{
    DBusError err;

    dbus_error_init(&err);
    dbus_bus_add_match(conn, "type='signal',sender='org.freedesktop.NetworkManager',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'", &err);
    dbus_error_free(&err);

    dbus_connection_add_filter(conn, onSignal, this, nullptr);
    refresh();
}

WifiState NmWifi::state() {
    return st;
}

void NmWifi::networksImpl(VisitorFace&& vis) {
    for (WifiNetwork* n : nets) {
        vis.visit(n);
    }
}

WifiNetwork* NmWifi::byPath(StringView path) {
    for (WifiNetwork* n : nets) {
        if (sv(n->path) == path) {
            return n;
        }
    }

    return nullptr;
}

Known* NmWifi::knownForSsid(StringView ssid) {
    for (Known* k : known) {
        if (sv(k->ssid) == ssid) {
            return k;
        }
    }

    return nullptr;
}

void NmWifi::notify() {
    for (WifiListener* l : c->wifiListeners) {
        l->wifiChanged();
    }

    c->scene->needsFrame = true;
}

DBusMessage* NmWifi::propGet(StringView path, const char* iface, const char* name) {
    Buffer pb(path);
    DBusMessage* msg = dbus_message_new_method_call(kNm, pb.cStr(), kProps, "Get");

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);

    DBusError err;

    dbus_error_init(&err);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);

    dbus_message_unref(msg);

    if (!reply) {
        dbus_error_free(&err);
    }

    return reply;
}

u32 NmWifi::propU32(StringView path, const char* iface, const char* name) {
    DBusMessage* r = propGet(path, iface, name);

    if (!r) {
        return 0;
    }

    DBusMessageIter it, var;

    dbus_message_iter_init(r, &it);
    dbus_message_iter_recurse(&it, &var);

    u32 v = 0;
    int t = dbus_message_iter_get_arg_type(&var);

    if (t == DBUS_TYPE_UINT32) {
        dbus_message_iter_get_basic(&var, &v);
    } else if (t == DBUS_TYPE_BYTE) {
        u8 b = 0;

        dbus_message_iter_get_basic(&var, &b);
        v = b;
    }

    dbus_message_unref(r);

    return v;
}

// an ssid property is a byte array wrapped in a variant
void NmWifi::variantSsid(DBusMessageIter* var, StringBuilder& out) {
    out.reset();

    if (dbus_message_iter_get_arg_type(var) != DBUS_TYPE_ARRAY) {
        return;
    }

    DBusMessageIter bytes;

    dbus_message_iter_recurse(var, &bytes);

    while (dbus_message_iter_get_arg_type(&bytes) == DBUS_TYPE_BYTE) {
        u8 b = 0;

        dbus_message_iter_get_basic(&bytes, &b);
        out << StringView(&b, (size_t)1);
        dbus_message_iter_next(&bytes);
    }
}

bool NmWifi::findWifiDevice() {
    devicePath.reset();

    DBusMessage* r = propGet(StringView(kNmPath), kNm, "Devices");

    if (!r) {
        return false;
    }

    DBusMessageIter it, var, arr;

    dbus_message_iter_init(r, &it);
    dbus_message_iter_recurse(&it, &var);

    if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&var, &arr);

        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_OBJECT_PATH && devicePath.empty()) {
            StringView d = iterStr(&arr);

            if (propU32(d, kDev, "DeviceType") == kDeviceWifi) {
                devicePath << d;
            }

            dbus_message_iter_next(&arr);
        }
    }

    dbus_message_unref(r);

    return !devicePath.empty();
}

void NmWifi::loadKnown() {
    for (Known* k : known) {
        knownAlloc.release(k);
    }

    known.clear();

    DBusMessage* r = propGet("/org/freedesktop/NetworkManager/Settings"_sv, "org.freedesktop.NetworkManager.Settings", "Connections");

    if (!r) {
        return;
    }

    DBusMessageIter it, var, arr;

    dbus_message_iter_init(r, &it);
    dbus_message_iter_recurse(&it, &var);

    if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&var, &arr);

        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_OBJECT_PATH) {
            StringView cp = iterStr(&arr);
            Buffer cpb(cp);
            DBusMessage* msg = dbus_message_new_method_call(kNm, cpb.cStr(), "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings");
            DBusError err;

            dbus_error_init(&err);

            DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);

            dbus_message_unref(msg);

            if (!reply) {
                dbus_error_free(&err);
                dbus_message_iter_next(&arr);

                continue;
            }

            // a{sa{sv}}: 802-11-wireless -> ssid (ay)
            DBusMessageIter rit, outer;

            dbus_message_iter_init(reply, &rit);
            dbus_message_iter_recurse(&rit, &outer);

            while (dbus_message_iter_get_arg_type(&outer) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter grp;

                dbus_message_iter_recurse(&outer, &grp);

                const char* gname = "";

                dbus_message_iter_get_basic(&grp, &gname);
                dbus_message_iter_next(&grp);

                if (StringView(gname) == "802-11-wireless"_sv) {
                    DBusMessageIter props;

                    dbus_message_iter_recurse(&grp, &props);

                    while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                        DBusMessageIter kv;

                        dbus_message_iter_recurse(&props, &kv);

                        const char* key = "";

                        dbus_message_iter_get_basic(&kv, &key);
                        dbus_message_iter_next(&kv);

                        if (StringView(key) == "ssid"_sv) {
                            DBusMessageIter var2;

                            dbus_message_iter_recurse(&kv, &var2);

                            Known* k = knownAlloc.make();

                            variantSsid(&var2, k->ssid);
                            k->path.reset();
                            k->path << cp;

                            if (k->ssid.empty()) {
                                knownAlloc.release(k);
                            } else {
                                known.pushBack(k);
                            }
                        }

                        dbus_message_iter_next(&props);
                    }
                }

                dbus_message_iter_next(&outer);
            }

            dbus_message_unref(reply);
            dbus_message_iter_next(&arr);
        }
    }

    dbus_message_unref(r);
}

void NmWifi::addAccessPoint(StringView apPath, StringView activePath) {
    DBusMessage* ssidR = propGet(apPath, kAp, "Ssid");

    if (!ssidR) {
        return;
    }

    StringBuilder ssid;

    {
        DBusMessageIter it, var;

        dbus_message_iter_init(ssidR, &it);
        dbus_message_iter_recurse(&it, &var);
        variantSsid(&var, ssid);
    }

    dbus_message_unref(ssidR);

    if (ssid.empty()) {
        return;
    }

    u32 strength = propU32(apPath, kAp, "Strength");
    u32 wpa = propU32(apPath, kAp, "WpaFlags");
    u32 rsn = propU32(apPath, kAp, "RsnFlags");

    WifiNetwork* n = netAlloc.make();

    n->name.reset();
    n->name << sv(ssid);
    n->path.reset();
    n->path << apPath;
    n->type.reset();
    n->type << ((wpa || rsn) ? "psk"_sv : "open"_sv);
    n->strength = (i16)strength; // NM reports 0..100
    n->connected = apPath == activePath;
    n->known = knownForSsid(sv(ssid)) != nullptr;
    nets.pushBack(n);
}

void NmWifi::refresh() {
    for (WifiNetwork* n : nets) {
        netAlloc.release(n);
    }

    nets.clear();

    if (!findWifiDevice()) {
        st = WifiState::unavailable;
        notify();

        return;
    }

    u32 devState = propU32(sv(devicePath), kDev, "State");

    st = WifiState::disconnected;

    if (devState >= kStateActivated) {
        st = WifiState::connected;
    } else if (devState >= kStateConfig && devState <= kStateNeedAuth) {
        st = WifiState::connecting;
    }

    loadKnown();

    StringBuilder active;

    if (DBusMessage* r = propGet(sv(devicePath), kWireless, "ActiveAccessPoint")) {
        DBusMessageIter it, var;

        dbus_message_iter_init(r, &it);
        dbus_message_iter_recurse(&it, &var);
        active << iterStr(&var);
        dbus_message_unref(r);
    }

    if (DBusMessage* r = propGet(sv(devicePath), kWireless, "AccessPoints")) {
        DBusMessageIter it, var, arr;

        dbus_message_iter_init(r, &it);
        dbus_message_iter_recurse(&it, &var);

        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&var, &arr);

            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_OBJECT_PATH) {
                addAccessPoint(iterStr(&arr), sv(active));
                dbus_message_iter_next(&arr);
            }
        }

        dbus_message_unref(r);
    }

    notify();
}

void NmWifi::scan() {
    Buffer db(sv(devicePath));
    DBusMessage* msg = dbus_message_new_method_call(kNm, db.cStr(), kWireless, "RequestScan");
    DBusMessageIter it, arr;

    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

void NmWifi::activate(StringView apPath) {
    WifiNetwork* n = byPath(apPath);
    Known* k = n ? knownForSsid(sv(n->name)) : nullptr;

    Buffer connBuf(k ? sv(k->path) : "/"_sv);
    Buffer devBuf(sv(devicePath));
    Buffer apBuf(apPath);
    const char* conn_o = connBuf.cStr();
    const char* dev_o = devBuf.cStr();
    const char* ap_o = apBuf.cStr();

    DBusMessage* msg = dbus_message_new_method_call(kNm, kNmPath, kNm, "ActivateConnection");

    dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &conn_o, DBUS_TYPE_OBJECT_PATH, &dev_o, DBUS_TYPE_OBJECT_PATH, &ap_o, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

static void appendStrEntry(DBusMessageIter* dict, const char* key, const char* val) {
    DBusMessageIter e, var;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&e, &var);
    dbus_message_iter_close_container(dict, &e);
}

void NmWifi::addAndActivate(StringView apPath, StringView ssid, StringView psk) {
    DBusMessage* msg = dbus_message_new_method_call(kNm, kNmPath, kNm, "AddAndActivateConnection");
    DBusMessageIter it, settings, grp, props;

    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sa{sv}}", &settings);

    Buffer ssidBuf(ssid);
    Buffer pskBuf(psk);

    // "connection": { id, type }
    {
        const char* g = "connection";

        dbus_message_iter_open_container(&settings, DBUS_TYPE_DICT_ENTRY, nullptr, &grp);
        dbus_message_iter_append_basic(&grp, DBUS_TYPE_STRING, &g);
        dbus_message_iter_open_container(&grp, DBUS_TYPE_ARRAY, "{sv}", &props);
        appendStrEntry(&props, "id", ssidBuf.cStr());
        appendStrEntry(&props, "type", "802-11-wireless");
        dbus_message_iter_close_container(&grp, &props);
        dbus_message_iter_close_container(&settings, &grp);
    }

    // "802-11-wireless": { ssid: ay }
    {
        const char* g = "802-11-wireless";

        dbus_message_iter_open_container(&settings, DBUS_TYPE_DICT_ENTRY, nullptr, &grp);
        dbus_message_iter_append_basic(&grp, DBUS_TYPE_STRING, &g);
        dbus_message_iter_open_container(&grp, DBUS_TYPE_ARRAY, "{sv}", &props);

        DBusMessageIter e, var, bytes;
        const char* key = "ssid";

        dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "ay", &var);
        dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &bytes);

        for (const u8* p = ssid.begin(); p < ssid.end(); p++) {
            dbus_message_iter_append_basic(&bytes, DBUS_TYPE_BYTE, p);
        }

        dbus_message_iter_close_container(&var, &bytes);
        dbus_message_iter_close_container(&e, &var);
        dbus_message_iter_close_container(&props, &e);
        dbus_message_iter_close_container(&grp, &props);
        dbus_message_iter_close_container(&settings, &grp);
    }

    // "802-11-wireless-security": { key-mgmt, psk } when there is a secret
    if (!psk.empty()) {
        const char* g = "802-11-wireless-security";

        dbus_message_iter_open_container(&settings, DBUS_TYPE_DICT_ENTRY, nullptr, &grp);
        dbus_message_iter_append_basic(&grp, DBUS_TYPE_STRING, &g);
        dbus_message_iter_open_container(&grp, DBUS_TYPE_ARRAY, "{sv}", &props);
        appendStrEntry(&props, "key-mgmt", "wpa-psk");
        appendStrEntry(&props, "psk", pskBuf.cStr());
        dbus_message_iter_close_container(&grp, &props);
        dbus_message_iter_close_container(&settings, &grp);
    }

    dbus_message_iter_close_container(&it, &settings);

    Buffer devBuf(sv(devicePath));
    Buffer apBuf(apPath);
    const char* dev_o = devBuf.cStr();
    const char* ap_o = apBuf.cStr();

    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &dev_o);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &ap_o);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

void NmWifi::connect(StringView path) {
    WifiNetwork* n = byPath(path);

    if (!n) {
        return;
    }

    // known or open networks activate straight away; a secured unknown one
    // needs the passphrase gathered up front (NM takes it in the settings)
    if (n->known || n->type == "open"_sv) {
        activate(path);

        return;
    }

    wantPass = true;
    passAp.reset();
    passAp << path;
    passSsid.reset();
    passSsid << sv(n->name);
    notify();
}

void NmWifi::disconnect() {
    Buffer db(sv(devicePath));
    DBusMessage* msg = dbus_message_new_method_call(kNm, db.cStr(), kDev, "Disconnect");

    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

void NmWifi::forget(StringView) {
    // deleting the saved connection needs its object path; the v1 ui offers
    // no forget button, so this stays a no-op
}

bool NmWifi::passphraseWanted() {
    return wantPass;
}

StringView NmWifi::passphraseFor() {
    return sv(passSsid);
}

void NmWifi::providePassphrase(StringView pw) {
    if (!wantPass) {
        return;
    }

    addAndActivate(sv(passAp), sv(passSsid), pw);
    wantPass = false;
    passAp.reset();
    passSsid.reset();
    notify();
}

void NmWifi::cancelPassphrase() {
    wantPass = false;
    passAp.reset();
    passSsid.reset();
    notify();
}

namespace {
    DBusHandlerResult onSignal(DBusConnection*, DBusMessage* msg, void* data) {
        auto* w = (NmWifi*)data;

        if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL && StringView(dbus_message_get_interface(msg)) == StringView(kProps)) {
            w->refresh();
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
}

Wifi* WifiNm::create(Composer& c) {
    if (!c.sysbus) {
        return nullptr;
    }

    DBusConnection* conn = c.sysbus->raw();
    DBusError err;

    dbus_error_init(&err);

    if (!dbus_bus_name_has_owner(conn, kNm, &err)) {
        dbus_error_free(&err);

        return nullptr;
    }

    sysO << "imway: wifi via NetworkManager"_sv << endL;

    return c.pool->make<NmWifi>(c, conn);
}
