#include "composer.h"
#include "notifications.h"
#include "dbus_conn.h"
#include "scene.h"
#include "util.h"

#include <ev.h>

#include <dbus/dbus.h>

#include <std/ios/sys.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr const char* kPath = "/org/freedesktop/Notifications";
    constexpr const char* kIface = "org.freedesktop.Notifications";
    constexpr double kDefaultExpiry = 5.0;

    // close reasons per the spec
    constexpr u32 kExpired = 1;
    constexpr u32 kDismissed = 2;
    constexpr u32 kClosedByCall = 3;

    struct NotificationsImpl;

    struct ToastImpl: public Toast {
        NotificationsImpl* srv = nullptr;
        ev_timer timer{};
    };

    void expiryCb(struct ev_loop*, ev_timer* w, int);
    DBusHandlerResult busMessage(DBusConnection* conn, DBusMessage* msg, void* data);

    struct NotificationsImpl: public Notifications {
        struct ev_loop* loop = nullptr;
        DBusConnection* conn = nullptr;
        Scene* scene = nullptr;
        ObjList<ToastImpl> toastAlloc;
        Vector<Toast*> toasts;
        u32 lastId = 0;

        NotificationsImpl(Composer& c);

        const Vector<Toast*>& active() override;
        void dismiss(u32 id) override;

        ToastImpl* byId(u32 id);
        void armTimer(ToastImpl& t, i32 expireMs);
        void close(u32 id, u32 reason);

        void notify(DBusMessage* msg);
        void closeCall(DBusMessage* msg);
        void capabilities(DBusMessage* msg);
        void serverInfo(DBusMessage* msg);
    };
}

NotificationsImpl::NotificationsImpl(Composer& c)
    : loop(c.loop)
    , conn(c.bus->raw())
    , scene(c.scene)
    , toastAlloc(c.pool)
{
    DBusError err;

    dbus_error_init(&err);

    int rc = dbus_bus_request_name(conn, kIface, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);

    if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        sysE << "imway: org.freedesktop.Notifications is taken ("_sv << rc << "), notifications disabled"_sv << endL;
        dbus_error_free(&err);

        return;
    }

    DBusObjectPathVTable vt{};

    vt.message_function = busMessage;
    dbus_connection_register_object_path(conn, kPath, &vt, this);
    sysO << "imway: notifications on the session bus"_sv << endL;
}

const Vector<Toast*>& NotificationsImpl::active() {
    return toasts;
}

void NotificationsImpl::dismiss(u32 id) {
    close(id, kDismissed);
}

ToastImpl* NotificationsImpl::byId(u32 id) {
    for (Toast* t : toasts) {
        if (t->id == id) {
            return (ToastImpl*)t;
        }
    }

    return nullptr;
}

void NotificationsImpl::armTimer(ToastImpl& t, i32 expireMs) {
    ev_timer_stop(loop, &t.timer);

    if (t.critical || expireMs == 0) {
        return; // sticky
    }

    double sec = expireMs > 0 ? expireMs / 1000.0 : kDefaultExpiry;

    ev_timer_init(&t.timer, expiryCb, sec, 0.);
    t.timer.data = &t;
    ev_timer_start(loop, &t.timer);
}

void NotificationsImpl::close(u32 id, u32 reason) {
    ToastImpl* t = byId(id);

    if (!t) {
        return;
    }

    ev_timer_stop(loop, &t->timer);
    removeOne(toasts, (Toast*)t);
    toastAlloc.release(t);

    DBusMessage* sig = dbus_message_new_signal(kPath, kIface, "NotificationClosed");

    dbus_message_append_args(sig, DBUS_TYPE_UINT32, &id, DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, sig, nullptr);
    dbus_message_unref(sig);
    scene->needsFrame = true;
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

    ToastImpl* t = replaces ? byId(replaces) : nullptr;

    if (!t) {
        t = toastAlloc.make();
        t->srv = this;
        t->id = ++lastId;
        toasts.pushBack(t);
    }

    t->app.reset();
    t->app << StringView(app);
    t->summary.reset();
    t->summary << StringView(summary);
    t->body.reset();
    t->body << StringView(body);
    t->icon.reset();
    t->icon << StringView(icon);
    t->critical = critical;
    armTimer(*t, expireMs);
    scene->needsFrame = true;

    DBusMessage* reply = dbus_message_new_method_return(msg);

    dbus_message_append_args(reply, DBUS_TYPE_UINT32, &t->id, DBUS_TYPE_INVALID);
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
}

void NotificationsImpl::closeCall(DBusMessage* msg) {
    u32 id = 0;

    if (dbus_message_get_args(msg, nullptr, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID)) {
        close(id, kClosedByCall);
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

    for (const char* c : caps) {
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &c);
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
    void expiryCb(struct ev_loop*, ev_timer* w, int) {
        auto* t = (ToastImpl*)w->data;

        t->srv->close(t->id, kExpired);
    }

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
