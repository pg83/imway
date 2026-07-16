#include "composer.h"
#include "wifi.h"
#include "wifi_iwd.h"
#include "dbus_conn.h"
#include "scene.h"
#include "util.h"

#include <dbus/dbus.h>

#include <std/ios/sys.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr const char* kService = "net.connman.iwd";
    constexpr const char* kAgentPath = "/imway/agent";

    struct IwdWifi;

    void onManaged(DBusPendingCall* pc, void* data);
    void onOrdered(DBusPendingCall* pc, void* data);
    DBusHandlerResult onSignal(DBusConnection*, DBusMessage* msg, void* data);
    DBusHandlerResult onAgent(DBusConnection*, DBusMessage* msg, void* data);

    // a network as GetManagedObjects sees it; the ordered list adds strength
    // and order on top
    struct NetInfo {
        StringBuilder path;
        StringBuilder name;
        StringBuilder type;
        bool connected = false;
        bool known = false;
    };

    struct IwdWifi: public Wifi {
        Composer* c = nullptr;
        DBusConnection* conn = nullptr;

        StringBuilder stationPath;
        WifiState st = WifiState::unavailable;
        WifiState notified = WifiState::unavailable;

        ObjList<NetInfo> infoAlloc;
        Vector<NetInfo*> infos;   // rebuilt on every GetManagedObjects

        ObjList<WifiNetwork> netAlloc;
        Vector<WifiNetwork*> nets; // the ordered, ui-facing list

        bool refreshInFlight = false;
        bool refreshAgain = false;

        // pending iwd Agent passphrase call, kept until the ui answers
        DBusMessage* passMsg = nullptr;
        StringBuilder passNet;

        IwdWifi(Composer& comp, DBusConnection* c);

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
        void callVoid(StringView path, StringView iface, StringView method);
        void notify();
        NetInfo* infoByPath(StringView path);

        void managedReply(DBusMessage* reply);
        void orderedReply(DBusMessage* reply);
        void agentCall(DBusMessage* msg);
        void registerAgent();
    };

    // read a string out of a variant iterator, empty on type mismatch
    StringView variantStr(DBusMessageIter* v) {
        if (dbus_message_iter_get_arg_type(v) != DBUS_TYPE_STRING) {
            return {};
        }

        const char* s = "";

        dbus_message_iter_get_basic(v, &s);

        return StringView(s);
    }

    bool variantBool(DBusMessageIter* v) {
        if (dbus_message_iter_get_arg_type(v) != DBUS_TYPE_BOOLEAN) {
            return false;
        }

        dbus_bool_t b = 0;

        dbus_message_iter_get_basic(v, &b);

        return b != 0;
    }
}

IwdWifi::IwdWifi(Composer& comp, DBusConnection* c)
    : c(&comp)
    , conn(c)
    , infoAlloc(comp.pool)
    , netAlloc(comp.pool)
{
    // fire-and-forget match registration (NULL error = no blocking round trip)
    dbus_bus_add_match(conn, "type='signal',sender='net.connman.iwd',interface='org.freedesktop.DBus.ObjectManager'", nullptr);
    dbus_bus_add_match(conn, "type='signal',sender='net.connman.iwd',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'", nullptr);
    dbus_connection_add_filter(conn, onSignal, this, nullptr);

    DBusObjectPathVTable vt{};

    vt.message_function = onAgent;
    dbus_connection_register_object_path(conn, kAgentPath, &vt, this);

    registerAgent();
    refresh();
}

WifiState IwdWifi::state() {
    return st;
}

void IwdWifi::networksImpl(VisitorFace&& vis) {
    for (WifiNetwork* n : nets) {
        vis.visit(n);
    }
}

NetInfo* IwdWifi::infoByPath(StringView path) {
    for (NetInfo* n : infos) {
        if (sv(n->path) == path) {
            return n;
        }
    }

    return nullptr;
}

void IwdWifi::notify() {
    StringView ssid;

    for (WifiNetwork* n : nets) {
        if (n->connected) {
            ssid = sv(n->name);

            break;
        }
    }

    wifiNotifyTransition(*c, notified, st, ssid);

    for (WifiListener* l : c->wifiListeners) {
        l->wifiChanged();
    }

    c->scene->needsFrame = true;
}

// GetManagedObjects, async; the reply rebuilds infos + station, then chains
// GetOrderedNetworks for order and strength
void IwdWifi::refresh() {
    if (refreshInFlight) {
        refreshAgain = true;

        return;
    }

    DBusMessage* msg = dbus_message_new_method_call(kService, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    DBusPendingCall* pc = nullptr;

    if (!dbus_connection_send_with_reply(conn, msg, &pc, 5000) || !pc) {
        dbus_message_unref(msg);

        return;
    }

    refreshInFlight = true;
    dbus_pending_call_set_notify(pc, onManaged, this, nullptr);
    dbus_message_unref(msg);
}

void IwdWifi::managedReply(DBusMessage* reply) {
    for (NetInfo* n : infos) {
        infoAlloc.release(n);
    }

    infos.clear();
    stationPath.reset();

    WifiState newState = WifiState::unavailable;
    StringView stationState;

    DBusMessageIter it;

    if (reply && dbus_message_iter_init(reply, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        DBusMessageIter objs;

        dbus_message_iter_recurse(&it, &objs);

        while (dbus_message_iter_get_arg_type(&objs) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter obj;

            dbus_message_iter_recurse(&objs, &obj);

            const char* path = "";

            dbus_message_iter_get_basic(&obj, &path);
            dbus_message_iter_next(&obj);

            NetInfo* net = nullptr;

            DBusMessageIter ifaces;

            dbus_message_iter_recurse(&obj, &ifaces);

            while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter iface;

                dbus_message_iter_recurse(&ifaces, &iface);

                const char* ifname = "";

                dbus_message_iter_get_basic(&iface, &ifname);
                dbus_message_iter_next(&iface);

                StringView in(ifname);
                bool isStation = in == "net.connman.iwd.Station"_sv;
                bool isNetwork = in == "net.connman.iwd.Network"_sv;

                if (isStation) {
                    stationPath.reset();
                    stationPath << StringView(path);
                }

                if (isNetwork) {
                    net = infoAlloc.make();
                    net->path.reset();
                    net->path << StringView(path);
                    net->name.reset();
                    net->type.reset();
                    net->connected = false;
                    net->known = false;
                    infos.pushBack(net);
                }

                if (!isStation && !isNetwork) {
                    dbus_message_iter_next(&ifaces);

                    continue;
                }

                DBusMessageIter props;

                dbus_message_iter_recurse(&iface, &props);

                while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter kv;

                    dbus_message_iter_recurse(&props, &kv);

                    const char* key = "";

                    dbus_message_iter_get_basic(&kv, &key);
                    dbus_message_iter_next(&kv);

                    DBusMessageIter var;

                    dbus_message_iter_recurse(&kv, &var);

                    StringView k(key);

                    if (isStation && k == "State"_sv) {
                        stationState = variantStr(&var);
                    } else if (isNetwork && net) {
                        if (k == "Name"_sv) {
                            net->name << variantStr(&var);
                        } else if (k == "Type"_sv) {
                            net->type << variantStr(&var);
                        } else if (k == "Connected"_sv) {
                            net->connected = variantBool(&var);
                        } else if (k == "KnownNetwork"_sv) {
                            net->known = dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_OBJECT_PATH;
                        }
                    }

                    dbus_message_iter_next(&props);
                }

                dbus_message_iter_next(&ifaces);
            }

            dbus_message_iter_next(&objs);
        }
    }

    if (!stationPath.empty()) {
        newState = WifiState::disconnected;

        if (stationState == "connected"_sv || stationState == "roaming"_sv) {
            newState = WifiState::connected;
        } else if (stationState == "connecting"_sv) {
            newState = WifiState::connecting;
        }
    }

    st = newState;

    if (stationPath.empty()) {
        for (WifiNetwork* n : nets) {
            netAlloc.release(n);
        }

        nets.clear();
        notify();

        return;
    }

    // chain GetOrderedNetworks for the ui list
    auto& call = sb();

    call << sv(stationPath);

    DBusMessage* msg = dbus_message_new_method_call(kService, Buffer(sv(call)).cStr(), "net.connman.iwd.Station", "GetOrderedNetworks");
    DBusPendingCall* pc = nullptr;

    if (dbus_connection_send_with_reply(conn, msg, &pc, 5000) && pc) {
        dbus_pending_call_set_notify(pc, onOrdered, this, nullptr);
    }

    dbus_message_unref(msg);
}

void IwdWifi::orderedReply(DBusMessage* reply) {
    for (WifiNetwork* n : nets) {
        netAlloc.release(n);
    }

    nets.clear();

    DBusMessageIter it;

    if (reply && dbus_message_iter_init(reply, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        DBusMessageIter arr;

        dbus_message_iter_recurse(&it, &arr);

        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
            DBusMessageIter e;

            dbus_message_iter_recurse(&arr, &e);

            const char* path = "";

            dbus_message_iter_get_basic(&e, &path);
            dbus_message_iter_next(&e);

            i16 strength = 0;

            if (dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_INT16) {
                dbus_message_iter_get_basic(&e, &strength);
            }

            if (NetInfo* info = infoByPath(StringView(path))) {
                WifiNetwork* n = netAlloc.make();

                n->name.reset();
                n->name << sv(info->name);
                n->path.reset();
                n->path << sv(info->path);
                n->type.reset();
                n->type << sv(info->type);
                // iwd gives dBm*100; map [-90,-50] dBm to 0..100 percent
                int dbm = strength / 100;
                int pct = (dbm + 90) * 5 / 2;

                n->strength = (i16)(pct < 0 ? 0 : pct > 100 ? 100 : pct);
                n->connected = info->connected;
                n->known = info->known;
                nets.pushBack(n);
            }

            dbus_message_iter_next(&arr);
        }
    }

    notify();

    if (refreshAgain) {
        refreshAgain = false;
        refresh();
    }
}

void IwdWifi::callVoid(StringView path, StringView iface, StringView method) {
    if (path.empty()) {
        return;
    }

    DBusMessage* msg = dbus_message_new_method_call(kService, Buffer(path).cStr(), Buffer(iface).cStr(), Buffer(method).cStr());

    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

void IwdWifi::scan() {
    callVoid(sv(stationPath), "net.connman.iwd.Station"_sv, "Scan"_sv);
}

void IwdWifi::connect(StringView path) {
    // Connect() drives the agent passphrase exchange itself; fire and forget
    callVoid(path, "net.connman.iwd.Network"_sv, "Connect"_sv);
}

void IwdWifi::disconnect() {
    callVoid(sv(stationPath), "net.connman.iwd.Station"_sv, "Disconnect"_sv);
}

void IwdWifi::forget(StringView) {
    // Forget lives on the KnownNetwork object, whose path we do not track
    // yet; the v1 ui offers no forget button, so this stays a no-op until
    // the known-network path is threaded through
}

bool IwdWifi::passphraseWanted() {
    return passMsg != nullptr;
}

StringView IwdWifi::passphraseFor() {
    return sv(passNet);
}

void IwdWifi::providePassphrase(StringView pw) {
    if (!passMsg) {
        return;
    }

    DBusMessage* reply = dbus_message_new_method_return(passMsg);
    Buffer p(pw);
    const char* pp = p.cStr();

    dbus_message_append_args(reply, DBUS_TYPE_STRING, &pp, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
    dbus_message_unref(passMsg);
    passMsg = nullptr;
    passNet.reset();
    notify();
}

void IwdWifi::cancelPassphrase() {
    if (!passMsg) {
        return;
    }

    DBusMessage* err = dbus_message_new_error(passMsg, "net.connman.iwd.Agent.Error.Canceled", "cancelled by user");

    dbus_connection_send(conn, err, nullptr);
    dbus_message_unref(err);
    dbus_message_unref(passMsg);
    passMsg = nullptr;
    passNet.reset();
    notify();
}

void IwdWifi::registerAgent() {
    DBusMessage* msg = dbus_message_new_method_call(kService, "/net/connman/iwd", "net.connman.iwd.AgentManager", "RegisterAgent");
    const char* p = kAgentPath;

    dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

void IwdWifi::agentCall(DBusMessage* msg) {
    if (dbus_message_is_method_call(msg, "net.connman.iwd.Agent", "RequestPassphrase")) {
        const char* netPath = "";

        dbus_message_get_args(msg, nullptr, DBUS_TYPE_OBJECT_PATH, &netPath, DBUS_TYPE_INVALID);

        if (passMsg) {
            dbus_message_unref(passMsg);
        }

        passMsg = dbus_message_ref(msg);
        passNet.reset();

        if (NetInfo* info = infoByPath(StringView(netPath))) {
            passNet << sv(info->name);
        }

        notify();
    } else if (dbus_message_is_method_call(msg, "net.connman.iwd.Agent", "Cancel")) {
        if (passMsg) {
            dbus_message_unref(passMsg);
            passMsg = nullptr;
            passNet.reset();
            notify();
        }

        DBusMessage* reply = dbus_message_new_method_return(msg);

        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);
    } else if (dbus_message_is_method_call(msg, "net.connman.iwd.Agent", "Release")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);

        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);
    }
}

namespace {
    void onManaged(DBusPendingCall* pc, void* data) {
        auto* w = (IwdWifi*)data;
        DBusMessage* reply = dbus_pending_call_steal_reply(pc);

        w->refreshInFlight = false;
        w->managedReply(reply);

        if (reply) {
            dbus_message_unref(reply);
        }

        dbus_pending_call_unref(pc);
    }

    void onOrdered(DBusPendingCall* pc, void* data) {
        auto* w = (IwdWifi*)data;
        DBusMessage* reply = dbus_pending_call_steal_reply(pc);

        w->orderedReply(reply);

        if (reply) {
            dbus_message_unref(reply);
        }

        dbus_pending_call_unref(pc);
    }

    DBusHandlerResult onSignal(DBusConnection*, DBusMessage* msg, void* data) {
        auto* w = (IwdWifi*)data;

        if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
            StringView iface(dbus_message_get_interface(msg));

            if (iface == "org.freedesktop.DBus.ObjectManager"_sv || iface == "org.freedesktop.DBus.Properties"_sv) {
                w->refresh();
            }
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    DBusHandlerResult onAgent(DBusConnection*, DBusMessage* msg, void* data) {
        auto* w = (IwdWifi*)data;

        if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
            w->agentCall(msg);

            return DBUS_HANDLER_RESULT_HANDLED;
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
}

Wifi* WifiIwd::create(Composer& c) {
    if (!c.sysbus) {
        return nullptr;
    }

    DBusConnection* conn = c.sysbus->raw();
    DBusError err;

    dbus_error_init(&err);

    // only claim iwd if it actually owns its name on the bus, so the
    // factory can fall through to the next provider
    if (!dbus_bus_name_has_owner(conn, "net.connman.iwd", &err)) {
        dbus_error_free(&err);

        return nullptr;
    }

    sysO << "imway: wifi via iwd"_sv << endL;

    return c.pool->make<IwdWifi>(c, conn);
}
