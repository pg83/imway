#include "wifi_nm.h"

#include "log.h"
#include "util.h"
#include "wifi.h"
#include "scene.h"
#include "composer.h"
#include "listener.h"
#include "dbus_conn.h"
#include "intr_list.h"

#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

#include <dbus/dbus.h>

using namespace stl;

// NetworkManager over dbus, fully async: every read is send_with_reply +
// a pending-call notify, never send_with_reply_and_block, so a slow or hung
// NM can never freeze the ev loop. NM has no ObjectManager, so a refresh is
// a tree of dependent queries with fan-out counters — Devices -> the wifi
// device -> known connections -> access points -> per-AP properties — that
// commits and notifies only when the last leaf lands. one refresh runs at a
// time (inFlight); signals arriving mid-refresh set `again` and restart once
// it finishes, the iwd pattern

namespace {
    constexpr const char* kNm = "org.freedesktop.NetworkManager";
    constexpr const char* kNmPath = "/org/freedesktop/NetworkManager";
    constexpr const char* kProps = "org.freedesktop.DBus.Properties";
    constexpr const char* kDev = "org.freedesktop.NetworkManager.Device";
    constexpr const char* kWireless = "org.freedesktop.NetworkManager.Device.Wireless";
    constexpr const char* kAp = "org.freedesktop.NetworkManager.AccessPoint";
    constexpr const char* kSettings = "org.freedesktop.NetworkManager.Settings";
    constexpr const char* kSettingsPath = "/org/freedesktop/NetworkManager/Settings";
    constexpr const char* kConnIface = "org.freedesktop.NetworkManager.Settings.Connection";

    constexpr u32 kDeviceWifi = 2;
    constexpr u32 kStateActivated = 100;
    constexpr u32 kStateConfig = 40;
    constexpr u32 kStateNeedAuth = 60;

    constexpr int kTimeout = 3000;

    struct NmWifi;

    // per-call context threading the object path (and owner) through an
    // async reply, since GetAll's reply does not echo which object it was
    struct Ctx {
        NmWifi* w = nullptr;
        StringBuilder path;
    };

    struct Known {
        StringBuilder ssid;
        StringBuilder path;
    };

    DBusHandlerResult onSignal(DBusConnection*, DBusMessage* msg, void* data);
    void devicesCb(DBusPendingCall* pc, void* data);
    void deviceCb(DBusPendingCall* pc, void* data);
    void connectionsCb(DBusPendingCall* pc, void* data);
    void connectionCb(DBusPendingCall* pc, void* data);
    void wirelessCb(DBusPendingCall* pc, void* data);
    void apCb(DBusPendingCall* pc, void* data);

    struct NmWifi: public Wifi {
        Composer* c = nullptr;
        DBusConnection* conn = nullptr;

        StringBuilder devicePath;
        WifiState st = WifiState::unavailable;
        WifiState notified = WifiState::unavailable;

        Vector<WifiNetwork*> nets; // committed, ui-facing
        Vector<Known*> known;      // committed

        // refresh epochs: everything a sequence builds — networks, known
        // connections, reply contexts, path scratch — lives in `building`;
        // the commit swaps it into `committed`, whose objects back nets and
        // known between refreshes, and the old generation dies whole. A
        // teardown mid-flight just deletes both arenas: in-flight reply
        // contexts die with their epoch, no tracking needed
        ObjPool* building = nullptr;
        ObjPool* committed = nullptr;

        bool wantPass = false;
        StringBuilder passAp;
        StringBuilder passSsid;

        // refresh sequencing
        bool inFlight = false;
        bool again = false;

        // scratch for the sequence in progress
        StringBuilder curDevice;
        u32 curState = 0;
        StringBuilder curActive;
        Vector<WifiNetwork*> netBuild;
        Vector<Known*> knownBuild;
        int devPending = 0;
        int knownPending = 0;
        int apPending = 0;

        NmWifi(Composer& comp, DBusConnection* c);
        ~NmWifi() noexcept;

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

        WifiNetwork* byPath(StringView path);
        Known* knownBuilt(StringView ssid);
        Known* knownForSsid(StringView ssid);
        void notify();

        // async plumbing
        bool call(DBusMessage* msg, DBusPendingCallNotifyFunction cb, void* data);
        bool getProp(StringView path, const char* iface, const char* prop, DBusPendingCallNotifyFunction cb, void* data);
        bool getAll(StringView path, const char* iface, DBusPendingCallNotifyFunction cb, void* data);

        // sequence steps
        void refresh();
        void devicesReply(DBusMessage* reply);
        void deviceReply(Ctx* cx, DBusMessage* reply);
        void deviceItemDone();
        void onDevicesDone();
        void connectionsReply(DBusMessage* reply);
        void connectionReply(Ctx* cx, DBusMessage* reply);
        void knownItemDone();
        void onKnownDone();
        void wirelessReply(DBusMessage* reply);
        void apReply(Ctx* cx, DBusMessage* reply);
        void apItemDone();
        void onApsDone();
        void finishSeq();
        void resetScratch();

        // actions (async fire-and-forget)
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

    void readSsid(DBusMessageIter* var, StringBuilder& out) {
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

    u32 readU32(DBusMessageIter* var) {
        u32 v = 0;
        int t = dbus_message_iter_get_arg_type(var);

        if (t == DBUS_TYPE_UINT32) {
            dbus_message_iter_get_basic(var, &v);
        } else if (t == DBUS_TYPE_BYTE) {
            u8 b = 0;

            dbus_message_iter_get_basic(var, &b);
            v = b;
        }

        return v;
    }

    // GetAll returns a{sv}; walk it, handing each (key, value-iter) to f
    template <typename F>
    void eachProp(DBusMessage* reply, F f) {
        DBusMessageIter it, arr;

        if (!reply || !dbus_message_iter_init(reply, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) {
            return;
        }

        dbus_message_iter_recurse(&it, &arr);

        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter e;

            dbus_message_iter_recurse(&arr, &e);

            const char* key = "";

            dbus_message_iter_get_basic(&e, &key);
            dbus_message_iter_next(&e);

            DBusMessageIter var;

            dbus_message_iter_recurse(&e, &var);
            f(StringView(key), &var);
            dbus_message_iter_next(&arr);
        }
    }
}

NmWifi::NmWifi(Composer& comp, DBusConnection* c)
    : c(&comp)
    , conn(c)
{
    // fire-and-forget match registration (NULL error = no blocking round trip)
    dbus_bus_add_match(conn, "type='signal',sender='org.freedesktop.NetworkManager',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'", nullptr);
    dbus_connection_add_filter(conn, onSignal, this, nullptr);
    refresh();
}

NmWifi::~NmWifi() noexcept {
    delete building;
    delete committed;
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

Known* NmWifi::knownBuilt(StringView ssid) {
    for (Known* k : knownBuild) {
        if (sv(k->ssid) == ssid) {
            return k;
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
    StringView ssid;

    for (WifiNetwork* n : nets) {
        if (n->connected) {
            ssid = sv(n->name);

            break;
        }
    }

    wifiNotifyTransition(*c, notified, st, ssid);

    forEach<Listener>(c->wifiListeners, [](Listener& listener) {
        listener.onListen();
    });

    c->scene->needsFrame = true;
}

bool NmWifi::call(DBusMessage* msg, DBusPendingCallNotifyFunction cb, void* data) {
    DBusPendingCall* pc = nullptr;

    if (!dbus_connection_send_with_reply(conn, msg, &pc, kTimeout) || !pc) {
        dbus_message_unref(msg);

        return false;
    }

    dbus_pending_call_set_notify(pc, cb, data, nullptr);
    dbus_message_unref(msg);

    return true;
}

bool NmWifi::getProp(StringView path, const char* iface, const char* prop, DBusPendingCallNotifyFunction cb, void* data) {
    Buffer pb(path);
    DBusMessage* msg = dbus_message_new_method_call(kNm, pb.cStr(), kProps, "Get");

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

    return call(msg, cb, data);
}

bool NmWifi::getAll(StringView path, const char* iface, DBusPendingCallNotifyFunction cb, void* data) {
    Buffer pb(path);
    DBusMessage* msg = dbus_message_new_method_call(kNm, pb.cStr(), kProps, "GetAll");

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);

    return call(msg, cb, data);
}

void NmWifi::resetScratch() {
    curDevice.reset();
    curState = 0;
    curActive.reset();
    netBuild.clear();
    knownBuild.clear();
    devPending = 0;
    knownPending = 0;
    apPending = 0;
}

void NmWifi::refresh() {
    if (inFlight) {
        again = true;

        return;
    }

    inFlight = true;
    building = ObjPool::fromMemoryRaw();
    resetScratch();

    // step 1: the device list
    if (!getProp(StringView(kNmPath), kNm, "Devices", devicesCb, this)) {
        finishSeq();
    }
}

void NmWifi::devicesReply(DBusMessage* reply) {
    Vector<StringBuilder*> paths; // transient; released below

    DBusMessageIter it, var, arr;

    if (reply && dbus_message_iter_init(reply, &it)) {
        dbus_message_iter_recurse(&it, &var);

        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&var, &arr);

            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_OBJECT_PATH) {
                StringBuilder* p = building->make<StringBuilder>();

                *p << iterStr(&arr);
                paths.pushBack(p);
                dbus_message_iter_next(&arr);
            }
        }
    }

    if (paths.empty()) {
        onDevicesDone();

        return;
    }

    devPending = (int)paths.length();

    for (StringBuilder* p : paths) {
        Ctx* cx = building->make<Ctx>();

        cx->w = this;
        cx->path << sv(*p);

        if (!getAll(sv(*p), kDev, deviceCb, cx)) {
            deviceItemDone();
        }
    }
}

void NmWifi::deviceReply(Ctx* cx, DBusMessage* reply) {
    u32 type = 0, state = 0;

    eachProp(reply, [&](StringView key, DBusMessageIter* var) {
        if (key == "DeviceType"_sv) {
            type = readU32(var);
        } else if (key == "State"_sv) {
            state = readU32(var);
        }
    });

    // first wifi device wins
    if (type == kDeviceWifi && curDevice.empty()) {
        curDevice << sv(cx->path);
        curState = state;
    }

    deviceItemDone();
}

void NmWifi::deviceItemDone() {
    if (--devPending <= 0) {
        onDevicesDone();
    }
}

void NmWifi::onDevicesDone() {
    if (curDevice.empty()) {
        st = WifiState::unavailable;

        // no wifi device: drop the list; the objects idle in `committed`
        // (known still lives there) until the next full commit
        nets.clear();
        notify();
        finishSeq();

        return;
    }

    st = WifiState::disconnected;

    if (curState >= kStateActivated) {
        st = WifiState::connected;
    } else if (curState >= kStateConfig && curState <= kStateNeedAuth) {
        st = WifiState::connecting;
    }

    // step 2: known connections
    if (!getProp(StringView(kSettingsPath), kSettings, "Connections", connectionsCb, this)) {
        onKnownDone();
    }
}

void NmWifi::connectionsReply(DBusMessage* reply) {
    Vector<StringBuilder*> paths; // transient; released below

    DBusMessageIter it, var, arr;

    if (reply && dbus_message_iter_init(reply, &it)) {
        dbus_message_iter_recurse(&it, &var);

        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&var, &arr);

            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_OBJECT_PATH) {
                StringBuilder* p = building->make<StringBuilder>();

                *p << iterStr(&arr);
                paths.pushBack(p);
                dbus_message_iter_next(&arr);
            }
        }
    }

    if (paths.empty()) {
        onKnownDone();

        return;
    }

    knownPending = (int)paths.length();

    for (StringBuilder* p : paths) {
        Ctx* cx = building->make<Ctx>();

        cx->w = this;
        cx->path << sv(*p);

        Buffer pb(sv(*p));
        DBusMessage* msg = dbus_message_new_method_call(kNm, pb.cStr(), kConnIface, "GetSettings");

        if (!call(msg, connectionCb, cx)) {
            knownItemDone();
        }
    }
}

void NmWifi::connectionReply(Ctx* cx, DBusMessage* reply) {
    // a{sa{sv}}: 802-11-wireless -> ssid (ay)
    DBusMessageIter it, outer;

    if (reply && dbus_message_iter_init(reply, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&it, &outer);

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
                        DBusMessageIter var;

                        dbus_message_iter_recurse(&kv, &var);

                        Known* k = building->make<Known>();

                        readSsid(&var, k->ssid);
                        k->path.reset();
                        k->path << sv(cx->path);

                        if (!k->ssid.empty()) {
                            knownBuild.pushBack(k);
                        }
                    }

                    dbus_message_iter_next(&props);
                }
            }

            dbus_message_iter_next(&outer);
        }
    }

    knownItemDone();
}

void NmWifi::knownItemDone() {
    if (--knownPending <= 0) {
        onKnownDone();
    }
}

void NmWifi::onKnownDone() {
    // step 3: the wireless device's access points
    if (!getAll(sv(curDevice), kWireless, wirelessCb, this)) {
        onApsDone();
    }
}

void NmWifi::wirelessReply(DBusMessage* reply) {
    Vector<StringBuilder*> aps; // transient; released below

    eachProp(reply, [&](StringView key, DBusMessageIter* var) {
        if (key == "ActiveAccessPoint"_sv) {
            curActive.reset();
            curActive << iterStr(var);
        } else if (key == "AccessPoints"_sv && dbus_message_iter_get_arg_type(var) == DBUS_TYPE_ARRAY) {
            DBusMessageIter arr;

            dbus_message_iter_recurse(var, &arr);

            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_OBJECT_PATH) {
                StringBuilder* p = building->make<StringBuilder>();

                *p << iterStr(&arr);
                aps.pushBack(p);
                dbus_message_iter_next(&arr);
            }
        }
    });

    if (aps.empty()) {
        onApsDone();

        return;
    }

    apPending = (int)aps.length();

    for (StringBuilder* p : aps) {
        Ctx* cx = building->make<Ctx>();

        cx->w = this;
        cx->path << sv(*p);

        if (!getAll(sv(*p), kAp, apCb, cx)) {
            apItemDone();
        }
    }
}

void NmWifi::apReply(Ctx* cx, DBusMessage* reply) {
    StringBuilder ssid;
    u32 strength = 0, wpa = 0, rsn = 0;

    eachProp(reply, [&](StringView key, DBusMessageIter* var) {
        if (key == "Ssid"_sv) {
            readSsid(var, ssid);
        } else if (key == "Strength"_sv) {
            strength = readU32(var);
        } else if (key == "WpaFlags"_sv) {
            wpa = readU32(var);
        } else if (key == "RsnFlags"_sv) {
            rsn = readU32(var);
        }
    });

    if (!ssid.empty()) {
        WifiNetwork* n = building->make<WifiNetwork>();

        n->name.reset();
        n->name << sv(ssid);
        n->path.reset();
        n->path << sv(cx->path);
        n->type.reset();
        n->type << ((wpa || rsn) ? "psk"_sv : "open"_sv);
        n->strength = (i16)strength; // NM reports 0..100
        n->connected = sv(cx->path) == sv(curActive);
        n->known = knownBuilt(sv(ssid)) != nullptr;
        netBuild.pushBack(n);
    }

    apItemDone();
}

void NmWifi::apItemDone() {
    if (--apPending <= 0) {
        onApsDone();
    }
}

void NmWifi::onApsDone() {
    // commit: the freshly built generation replaces the previous one whole
    nets.clear();

    for (WifiNetwork* n : netBuild) {
        nets.pushBack(n);
    }

    netBuild.clear();
    known.clear();

    for (Known* k : knownBuild) {
        known.pushBack(k);
    }

    knownBuild.clear();
    delete committed;
    committed = building;
    building = nullptr;
    notify();
    finishSeq();
}

void NmWifi::finishSeq() {
    // an aborted sequence still holds its epoch; a committed one left nullptr
    delete building;
    building = nullptr;
    inFlight = false;

    if (again) {
        again = false;
        refresh();
    }
}

void NmWifi::scan() {
    if (curDevice.empty()) {
        return;
    }

    Buffer db(sv(curDevice));
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
    Buffer devBuf(sv(curDevice));
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

    Buffer devBuf(sv(curDevice));
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
    if (curDevice.empty()) {
        return;
    }

    Buffer db(sv(curDevice));
    DBusMessage* msg = dbus_message_new_method_call(kNm, db.cStr(), kDev, "Disconnect");

    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

void NmWifi::forget(StringView) {
    // the v1 ui offers no forget button
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
    DBusMessage* steal(DBusPendingCall* pc) {
        DBusMessage* reply = dbus_pending_call_steal_reply(pc);

        dbus_pending_call_unref(pc);

        // an error reply is not a value reply; treat as empty
        if (reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
            dbus_message_unref(reply);

            return nullptr;
        }

        return reply;
    }

    void devicesCb(DBusPendingCall* pc, void* data) {
        auto* w = (NmWifi*)data;
        DBusMessage* reply = steal(pc);

        w->devicesReply(reply);

        if (reply) {
            dbus_message_unref(reply);
        }
    }

    void deviceCb(DBusPendingCall* pc, void* data) {
        auto* cx = (Ctx*)data;
        NmWifi* w = cx->w;
        DBusMessage* reply = steal(pc);

        w->deviceReply(cx, reply);

        if (reply) {
            dbus_message_unref(reply);
        }
    }

    void connectionsCb(DBusPendingCall* pc, void* data) {
        auto* w = (NmWifi*)data;
        DBusMessage* reply = steal(pc);

        w->connectionsReply(reply);

        if (reply) {
            dbus_message_unref(reply);
        }
    }

    void connectionCb(DBusPendingCall* pc, void* data) {
        auto* cx = (Ctx*)data;
        NmWifi* w = cx->w;
        DBusMessage* reply = steal(pc);

        w->connectionReply(cx, reply);

        if (reply) {
            dbus_message_unref(reply);
        }
    }

    void wirelessCb(DBusPendingCall* pc, void* data) {
        auto* w = (NmWifi*)data;
        DBusMessage* reply = steal(pc);

        w->wirelessReply(reply);

        if (reply) {
            dbus_message_unref(reply);
        }
    }

    void apCb(DBusPendingCall* pc, void* data) {
        auto* cx = (Ctx*)data;
        NmWifi* w = cx->w;
        DBusMessage* reply = steal(pc);

        w->apReply(cx, reply);

        if (reply) {
            dbus_message_unref(reply);
        }
    }

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

    *(c.log) << "imway: wifi via NetworkManager"_sv << endL;

    return c.pool->make<NmWifi>(c, conn);
}
