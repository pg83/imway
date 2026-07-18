#include "composer.h"
#include "listener.h"
#include "notifications.h"
#include "notifier.h"
#include "dbus_conn.h"
#include "util.h"

#include <dbus/dbus.h>

#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr const char* kPath = "/org/freedesktop/Notifications";
    constexpr const char* kIface = "org.freedesktop.Notifications";

    constexpr u32 kClosedByCall = 3;

    struct NotificationsImpl;

    DBusHandlerResult busMessage(DBusConnection* conn, DBusMessage* msg, void* data);

    struct NotificationsImpl: public Notifications, public Listener {
        DBusConnection* conn = nullptr;
        Notifier* notifier = nullptr;

        NotificationsImpl(Composer& c);

        void onListen(void* arg) override;

        void notify(DBusMessage* msg);
        void closeCall(DBusMessage* msg);
        void capabilities(DBusMessage* msg);
        void serverInfo(DBusMessage* msg);
    };
}

NotificationsImpl::NotificationsImpl(Composer& c)
    : conn(c.bus->raw())
    , notifier(c.notifier)
{
    DBusError err;

    dbus_error_init(&err);

    int rc = dbus_bus_request_name(conn, kIface, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);

    if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        sysE << "imway: org.freedesktop.Notifications is taken ("_sv << rc << "), dbus notifications disabled"_sv << endL;
        dbus_error_free(&err);

        return;
    }

    DBusObjectPathVTable vt{};

    vt.message_function = busMessage;
    dbus_connection_register_object_path(conn, kPath, &vt, this);
    c.notifierListeners.pushBack((Listener*)this);
    sysO << "imway: notifications on the session bus"_sv << endL;
}

void NotificationsImpl::onListen(void* arg) {
    auto& event = *(NotificationClosedEvent*)arg;
    u32 id = event.id;
    u32 reason = event.reason;
    DBusMessage* sig = dbus_message_new_signal(kPath, kIface, "NotificationClosed");

    dbus_message_append_args(sig, DBUS_TYPE_UINT32, &id, DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, sig, nullptr);
    dbus_message_unref(sig);
}

void NotificationsImpl::notify(DBusMessage* msg) {
    DBusMessageIter it;

    if (!dbus_message_iter_init(msg, &it)) {
        return;
    }

    const char* app = "";
    u32 replaces = 0;
    const char* icon = "";
    const char* summary = "";
    const char* body = "";
    i32 expireMs = -1;
    bool critical = false;

    // susssasa{sv}i, in order; bail on shape mismatch
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) {
        return;
    }

    dbus_message_iter_get_basic(&it, &app);
    dbus_message_iter_next(&it);

    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_UINT32) {
        return;
    }

    dbus_message_iter_get_basic(&it, &replaces);
    dbus_message_iter_next(&it);

    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) {
        return;
    }

    dbus_message_iter_get_basic(&it, &icon);
    dbus_message_iter_next(&it);

    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) {
        return;
    }

    dbus_message_iter_get_basic(&it, &summary);
    dbus_message_iter_next(&it);

    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) {
        return;
    }

    dbus_message_iter_get_basic(&it, &body);
    dbus_message_iter_next(&it);

    // actions: skipped (no buttons in v1, capabilities do not announce them)
    if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_next(&it);
    }

    // hints: only urgency matters
    if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        DBusMessageIter dict;

        dbus_message_iter_recurse(&it, &dict);

        while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter kv;

            dbus_message_iter_recurse(&dict, &kv);

            const char* key = "";

            dbus_message_iter_get_basic(&kv, &key);
            dbus_message_iter_next(&kv);

            if (StringView(key) == "urgency"_sv && dbus_message_iter_get_arg_type(&kv) == DBUS_TYPE_VARIANT) {
                DBusMessageIter var;

                dbus_message_iter_recurse(&kv, &var);

                if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_BYTE) {
                    unsigned char u = 0;

                    dbus_message_iter_get_basic(&var, &u);
                    critical = u >= 2;
                }
            }

            dbus_message_iter_next(&dict);
        }

        dbus_message_iter_next(&it);
    }

    if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_INT32) {
        dbus_message_iter_get_basic(&it, &expireMs);
    }

    Post p;

    p.app = StringView(app);
    p.summary = StringView(summary);
    p.body = StringView(body);
    p.icon = StringView(icon);
    p.critical = critical;
    p.fromBus = true;
    p.expireMs = expireMs;
    p.replacesId = replaces;

    u32 id = notifier->post(p);

    DBusMessage* reply = dbus_message_new_method_return(msg);

    dbus_message_append_args(reply, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

void NotificationsImpl::closeCall(DBusMessage* msg) {
    u32 id = 0;

    if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID)) {
        notifier->close(id, kClosedByCall);
    }

    DBusMessage* reply = dbus_message_new_method_return(msg);

    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

void NotificationsImpl::capabilities(DBusMessage* msg) {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter it, arr;
    const char* caps[] = {"body"};

    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);

    for (const char* cap : caps) {
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &cap);
    }

    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

void NotificationsImpl::serverInfo(DBusMessage* msg) {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    const char* name = "imway";
    const char* vendor = "imway";
    const char* version = "0.1";
    const char* spec = "1.2";

    dbus_message_append_args(reply, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &vendor, DBUS_TYPE_STRING, &version, DBUS_TYPE_STRING, &spec, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

namespace {
    DBusHandlerResult busMessage(DBusConnection*, DBusMessage* msg, void* data) {
        auto* impl = (NotificationsImpl*)data;

        if (dbus_message_is_method_call(msg, kIface, "Notify")) {
            impl->notify(msg);
        } else if (dbus_message_is_method_call(msg, kIface, "CloseNotification")) {
            impl->closeCall(msg);
        } else if (dbus_message_is_method_call(msg, kIface, "GetCapabilities")) {
            impl->capabilities(msg);
        } else if (dbus_message_is_method_call(msg, kIface, "GetServerInformation")) {
            impl->serverInfo(msg);
        } else {
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    }
}

Notifications* Notifications::create(Composer& c) {
    return c.pool->make<NotificationsImpl>(c);
}
