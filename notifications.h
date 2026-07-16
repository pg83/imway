#pragma once

struct Composer;

// the org.freedesktop.Notifications dbus service: parses Notify into the
// shared Notifier store, answers CloseNotification/GetCapabilities/
// GetServerInformation, and (as the store's listener) turns a bus-origin
// toast leaving the screen into the spec's NotificationClosed signal
struct Notifications {
    // nullptr when there is no session bus or the name is taken
    static Notifications* create(Composer& c);
};
