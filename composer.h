#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct DBusConn;
struct IconPool;
struct IconStore;
struct IconStoreListener;
struct Keyboard;
struct Mixer;
struct Notifications;
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
    DBusConn* bus = nullptr;
    Notifications* notes = nullptr;
    Wayland* wayland = nullptr;
    Renderer* renderer = nullptr;
    Mixer* mixer = nullptr;

    // listener slots: interfaces stay narrow, the slot solves the creation
    // order — the producer reads it at event time, the consumer fills it
    // whenever it comes up
    IconStoreListener* iconListener = nullptr;
};
