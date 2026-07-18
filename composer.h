#pragma once

#include <std/lib/list.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct DBusConn;
struct IconPool;
struct IconResolver;
struct FrameListener;
struct Wifi;
struct IconStore;
struct Keyboard;
struct Mixer;
struct Notifications;
struct Notifier;
struct NotifierListener;
struct Output;
struct Renderer;
struct Scene;
struct Session;
struct Wayland;

// the wiring board: main owns one, fills the fields as the entities come
// up, everyone else stores the reference. rules: a constructor may keep the
// reference but only read fields created before it; everything else
// dereferences at use time, when the graph is complete; nullable fields
// (bus, notes, session, mixer) stay nullable forever — a missing subsystem
// is a normal mode, check on every use
struct Composer {
    stl::ObjPool* pool = nullptr;
    struct ev_loop* loop = nullptr;
    Scene* scene = nullptr;

    Session* session = nullptr;
    Output* output = nullptr;
    Keyboard* kb = nullptr;
    IconPool* iconPool = nullptr;
    IconStore* icons = nullptr;
    IconResolver* iconResolver = nullptr;
    DBusConn* bus = nullptr;
    Notifier* notifier = nullptr;
    Notifications* notes = nullptr;
    Wayland* wayland = nullptr;
    Renderer* renderer = nullptr;
    Mixer* mixer = nullptr;
    DBusConn* sysbus = nullptr;
    Wifi* wifi = nullptr;

    // listener slots solve the creation order: subscribers link themselves
    // whenever they come up, producers walk the intrusive lists at event time
    stl::IntrusiveList iconListeners;
    stl::IntrusiveList mixerListeners;
    stl::IntrusiveList wifiListeners;
    stl::IntrusiveList notifierListeners;
    stl::IntrusiveList sessionEnabledListeners;
    stl::IntrusiveList sessionDisabledListeners;
    stl::IntrusiveList frameListeners;
};
