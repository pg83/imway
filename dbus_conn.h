#pragma once

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct DBusConnection;

// session bus connection glued onto the libev loop: libdbus watches ride
// ev_io, its timeouts ride ev_timer, and message dispatch drains in an
// ev_prepare at the top of every loop iteration (libdbus forbids
// dispatching from inside a watch handler)
struct DBusConn {
    virtual DBusConnection* raw() = 0;

    // UpdateActivationEnvironment on the bus: dbus-activated services get
    // the real value (e.g. WAYLAND_DISPLAY as actually bound), not whatever
    // the daemon inherited at its start
    virtual void setActivationEnv(stl::StringView key, stl::StringView value) = 0;

    // nullptr when the session bus is unreachable; the desktop just runs
    // without dbus services then
    static DBusConn* create(stl::ObjPool* pool, struct ev_loop* loop);
};
