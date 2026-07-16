#pragma once

#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct DBusConn;
struct Scene;

// one on-screen notification; icon is the raw Icon= value from the client
// (a name or a path), resolved through the icon store at draw time
struct Toast {
    u32 id = 0;
    stl::StringBuilder app;
    stl::StringBuilder summary;
    stl::StringBuilder body;
    stl::StringBuilder icon;
    bool critical = false; // urgency 2: never expires, accented
};

// org.freedesktop.Notifications on the session bus: Notify lands here as a
// Toast with an expiry timer, the renderer draws whatever active() holds,
// a click comes back as dismiss(); every close emits NotificationClosed
struct Notifications {
    virtual const stl::Vector<Toast*>& active() = 0;
    virtual void dismiss(u32 id) = 0;

    static Notifications* create(stl::ObjPool* pool, struct ev_loop* loop, DBusConn& bus, Scene& scene);
};
