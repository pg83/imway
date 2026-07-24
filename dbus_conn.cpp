#include "dbus_conn.h"

#include "log.h"
#include "pooled_ev.h"
#include "small_obj_allocator.h"
#include "util.h"

#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

#include <dbus/dbus.h>
#include <ev.h>

using namespace stl;

namespace {
    struct DBusConnImpl;

    // one libdbus watch = one ev_io; a single fd may carry separate read
    // and write watches, so the box wraps the watch, not the fd
    struct WatchBox {
        ev_io io{};
        DBusConnImpl* conn = nullptr;
        DBusWatch* watch = nullptr;
    };

    struct TimeoutBox {
        ev_timer timer{};
        DBusConnImpl* conn = nullptr;
        DBusTimeout* timeout = nullptr;
    };

    void watchCb(struct ev_loop*, ev_io* w, int revents);
    void timeoutCb(struct ev_loop*, ev_timer* w, int);
    void prepareCb(struct ev_loop*, ev_prepare* w, int);

    dbus_bool_t watchAdd(DBusWatch* w, void* data);
    void watchRemove(DBusWatch* w, void* data);
    void watchToggle(DBusWatch* w, void* data);
    dbus_bool_t timeoutAdd(DBusTimeout* t, void* data);
    void timeoutRemove(DBusTimeout* t, void* data);
    void timeoutToggle(DBusTimeout* t, void* data);

    struct DBusConnImpl: public DBusConn {
        struct ev_loop* loop = nullptr;
        DBusConnection* conn = nullptr;
        SmallObjAllocator* alloc = nullptr;

        DBusConnImpl(ObjPool* pool, SmallObjAllocator* a, struct ev_loop* evLoop, DBusConnection* c);
        // closing walks the watch/timeout callbacks back into this impl, so
        // it must happen in the destructor, while the impl is still alive;
        // the pooled prepare hook stops afterwards, which is harmless
        ~DBusConnImpl() noexcept;

        DBusConnection* raw() override;
        void setActivationEnv(StringView key, StringView value) override;
    };
}

DBusConnImpl::DBusConnImpl(ObjPool* pool, SmallObjAllocator* a, struct ev_loop* evLoop, DBusConnection* c)
    : loop(evLoop)
    , conn(c)
    , alloc(a)
{
    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    dbus_connection_set_watch_functions(conn, watchAdd, watchRemove, watchToggle, this, nullptr);
    dbus_connection_set_timeout_functions(conn, timeoutAdd, timeoutRemove, timeoutToggle, this, nullptr);

    ev_prepare* prepare = createEvPrepare(*pool, loop);

    ev_prepare_init(prepare, prepareCb);
    prepare->data = this;
    ev_prepare_start(loop, prepare);
}

DBusConnImpl::~DBusConnImpl() noexcept {
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
}

DBusConnection* DBusConnImpl::raw() {
    return conn;
}

void DBusConnImpl::setActivationEnv(StringView key, StringView value) {
    DBusMessage* msg = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "UpdateActivationEnvironment");
    DBusMessageIter it, arr, entry;

    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{ss}", &arr);
    dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);

    // materialized at the C boundary, alive until the send
    Buffer k(key), v(value);
    const char* kp = k.cStr();
    const char* vp = v.cStr();

    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &kp);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &vp);
    dbus_message_iter_close_container(&arr, &entry);
    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(conn, msg, nullptr);
    dbus_message_unref(msg);
}

namespace {
    void watchCb(struct ev_loop*, ev_io* w, int revents) {
        auto* box = (WatchBox*)w->data;
        unsigned flags = 0;

        if (revents & EV_READ) {
            flags |= DBUS_WATCH_READABLE;
        }

        if (revents & EV_WRITE) {
            flags |= DBUS_WATCH_WRITABLE;
        }

        // dispatch happens in the prepare watcher, this only moves bytes
        dbus_watch_handle(box->watch, flags);
    }

    void timeoutCb(struct ev_loop*, ev_timer* w, int) {
        dbus_timeout_handle(((TimeoutBox*)w->data)->timeout);
    }

    void prepareCb(struct ev_loop*, ev_prepare* w, int) {
        auto* impl = (DBusConnImpl*)w->data;

        while (dbus_connection_dispatch(impl->conn) == DBUS_DISPATCH_DATA_REMAINS) {
        }
    }

    int watchEvents(DBusWatch* w) {
        unsigned flags = dbus_watch_get_flags(w);
        int ev = 0;

        if (flags & DBUS_WATCH_READABLE) {
            ev |= EV_READ;
        }

        if (flags & DBUS_WATCH_WRITABLE) {
            ev |= EV_WRITE;
        }

        return ev;
    }

    dbus_bool_t watchAdd(DBusWatch* w, void* data) {
        auto* impl = (DBusConnImpl*)data;
        WatchBox* box = impl->alloc->make<WatchBox>();

        box->conn = impl;
        box->watch = w;
        ev_io_init(&box->io, watchCb, dbus_watch_get_unix_fd(w), watchEvents(w));
        box->io.data = box;
        dbus_watch_set_data(w, box, nullptr);

        if (dbus_watch_get_enabled(w)) {
            ev_io_start(impl->loop, &box->io);
        }

        return TRUE;
    }

    void watchRemove(DBusWatch* w, void* data) {
        auto* impl = (DBusConnImpl*)data;
        auto* box = (WatchBox*)dbus_watch_get_data(w);

        if (box) {
            ev_io_stop(impl->loop, &box->io);
            dbus_watch_set_data(w, nullptr, nullptr);
            impl->alloc->release(box);
        }
    }

    void watchToggle(DBusWatch* w, void* data) {
        auto* impl = (DBusConnImpl*)data;
        auto* box = (WatchBox*)dbus_watch_get_data(w);

        if (!box) {
            return;
        }

        ev_io_stop(impl->loop, &box->io);
        ev_io_set(&box->io, dbus_watch_get_unix_fd(w), watchEvents(w));

        if (dbus_watch_get_enabled(w)) {
            ev_io_start(impl->loop, &box->io);
        }
    }

    dbus_bool_t timeoutAdd(DBusTimeout* t, void* data) {
        auto* impl = (DBusConnImpl*)data;
        TimeoutBox* box = impl->alloc->make<TimeoutBox>();
        double sec = dbus_timeout_get_interval(t) / 1000.0;

        box->conn = impl;
        box->timeout = t;
        ev_timer_init(&box->timer, timeoutCb, sec, sec);
        box->timer.data = box;
        dbus_timeout_set_data(t, box, nullptr);

        if (dbus_timeout_get_enabled(t)) {
            ev_timer_start(impl->loop, &box->timer);
        }

        return TRUE;
    }

    void timeoutRemove(DBusTimeout* t, void* data) {
        auto* impl = (DBusConnImpl*)data;
        auto* box = (TimeoutBox*)dbus_timeout_get_data(t);

        if (box) {
            ev_timer_stop(impl->loop, &box->timer);
            dbus_timeout_set_data(t, nullptr, nullptr);
            impl->alloc->release(box);
        }
    }

    void timeoutToggle(DBusTimeout* t, void* data) {
        auto* impl = (DBusConnImpl*)data;
        auto* box = (TimeoutBox*)dbus_timeout_get_data(t);

        if (!box) {
            return;
        }

        ev_timer_stop(impl->loop, &box->timer);

        if (dbus_timeout_get_enabled(t)) {
            double sec = dbus_timeout_get_interval(t) / 1000.0;

            ev_timer_set(&box->timer, sec, sec);
            ev_timer_start(impl->loop, &box->timer);
        }
    }
}

DBusConn* DBusConn::create(ObjPool* pool, SmallObjAllocator* alloc, struct ev_loop* loop, Log& log, bool system) {
    DBusError err;

    dbus_error_init(&err);

    DBusConnection* conn = dbus_bus_get_private(system ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION, &err);

    if (!conn) {
        log << "imway: no "_sv << (system ? "system"_sv : "session"_sv) << " bus ("_sv << (err.message ? err.message : "?") << "), dbus services disabled"_sv << endL;
        dbus_error_free(&err);

        return nullptr;
    }

    return pool->make<DBusConnImpl>(pool, alloc, loop, conn);
}
