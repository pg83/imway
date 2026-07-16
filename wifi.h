#pragma once

#include "visitor.h"

#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/sys/types.h>

struct Composer;

// any state change — scan results, connection progress, a passphrase
// request — lands here; the wifi dialog is the subscriber
struct WifiListener {
    virtual void wifiChanged() = 0;
};

// one visible network; path is the dbus object path, the handle for
// connect/forget
struct WifiNetwork {
    stl::StringBuilder name;
    stl::StringBuilder path;
    stl::StringBuilder type; // "psk", "open", "8021x"
    i16 strength = 0;        // dBm * 100, higher is stronger
    bool connected = false;
    bool known = false;
};

enum class WifiState {
    unavailable, // no station (no adapter, or rfkill)
    disconnected,
    scanning,
    connecting,
    connected,
};

// wifi via iwd (net.connman.iwd) on the system bus; the interface stays
// provider-agnostic. connect() may trigger a passphrase request, answered
// by providePassphrase() from the ui dialog — that reply completes iwd's
// pending Agent call
struct Wifi {
    virtual WifiState state() = 0;

    // enumerate the visible networks, signal-ordered
    virtual void networksImpl(stl::VisitorFace&& vis) = 0;

    template <typename F>
    void networks(F f) {
        networksImpl(visitEach<WifiNetwork>(f));
    }

    virtual void scan() = 0;
    virtual void connect(stl::StringView path) = 0;
    virtual void disconnect() = 0;
    virtual void forget(stl::StringView path) = 0;

    // set while iwd waits for a secret; the dialog shows an input and calls
    // providePassphrase (reply) or cancelPassphrase (error back to iwd)
    virtual bool passphraseWanted() = 0;
    virtual stl::StringView passphraseFor() = 0;
    virtual void providePassphrase(stl::StringView pw) = 0;
    virtual void cancelPassphrase() = 0;

    // tries the providers in order — iwd, then NetworkManager — picking
    // whichever actually owns its name on the system bus; nullptr if
    // neither is running (or there is no system bus)
    static Wifi* create(Composer& c);
};

// posts a "connected to <ssid>" / "disconnected" notification on the stable
// state edge; each provider calls it from notify(), `last` is per-provider
void wifiNotifyTransition(Composer& c, WifiState& last, WifiState now, stl::StringView ssid);
