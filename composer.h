#pragma once

#include "theme.h"

#include <std/lib/list.h>
#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
    struct ThreadPool;
}

struct ev_loop;

struct DBusConn;
struct Icon;
struct IconPool;
struct IconResolver;
struct Filter;
struct FrameCapture;
struct Wifi;
struct Keyboard;
struct Mixer;
struct Notifications;
struct Notifier;
struct Output;
struct Device;
struct Renderer;
struct Scene;
struct Session;
struct SmallObjAllocator;
struct StatusNotifier;
struct Supervisor;
struct Wayland;
struct InputSink;

// the wiring board: main creates one as the first object of the root pool
// (so it dies after every subsystem holding the reference) and fills the
// fields as the entities come up. rules: a constructor may keep the
// reference but only read fields created before it; everything else
// dereferences at use time, when the graph is complete; nullable fields
// (bus, notes, session, mixer) stay nullable forever — a missing subsystem
// is a normal mode, check on every use
struct Composer {
    Composer(stl::ObjPool* pool);

    // walk iconProviders, first non-null answer wins; the string form hashes
    // once for the whole registry walk, the symbol form is for callers with
    // a precomputed key (see IconProvider in icon.h for the contract)
    Icon* findIcon(stl::StringView id);
    Icon* findIcon(u64 sym, stl::StringView id = {});

    Theme theme;
    stl::ObjPool* pool = nullptr;
    SmallObjAllocator* alloc = nullptr;
    struct ev_loop* loop = nullptr;
    stl::ThreadPool* offload = nullptr;
    Scene* scene = nullptr;

    Session* session = nullptr;
    Supervisor* supervisor = nullptr;
    Device* device = nullptr;
    Output* output = nullptr;
    Keyboard* kb = nullptr;
    IconPool* iconPool = nullptr;
    IconResolver* iconResolver = nullptr;
    DBusConn* bus = nullptr;
    Notifier* notifier = nullptr;
    Notifications* notes = nullptr;
    StatusNotifier* statusNotifier = nullptr;
    Wayland* wayland = nullptr;
    Renderer* renderer = nullptr;
    // the renderer registers itself here; wayland's copy-capture reads it
    FrameCapture* frameCapture = nullptr;
    Mixer* mixer = nullptr;
    DBusConn* sysbus = nullptr;
    Wifi* wifi = nullptr;
    InputSink* entry = nullptr;

    // listener slots solve the creation order: subscribers link themselves
    // whenever they come up, producers walk the intrusive lists at event time
    // IconProvider registry, walked by findIcon in registration order
    stl::IntrusiveList iconProviders;
    stl::IntrusiveList mixerListeners;
    stl::IntrusiveList wifiListeners;
    stl::IntrusiveList notifierListeners;
    stl::IntrusiveList sessionEnabledListeners;
    stl::IntrusiveList sessionDisabledListeners;
    stl::IntrusiveList frameListeners;
    // input producers call entry; it walks this list in order and stops at
    // the first sink which returns true
    stl::IntrusiveList inputSinks;
    stl::IntrusiveList filters;
};
