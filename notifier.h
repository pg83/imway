#pragma once

#include "visitor.h"

#include <std/lib/list.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/types.h>

struct Composer;

// one notification; icon is a raw Icon= value (name or path), resolved
// through the icon store at draw time. onScreen toasts show, the rest live
// in history until trimmed
// the node links it into the notifier's history list
struct Toast: stl::IntrusiveNode {
    u32 id = 0;
    stl::StringBuilder app;
    stl::StringBuilder summary;
    stl::StringBuilder body;
    stl::StringBuilder icon;
    bool critical = false;  // urgency 2: never expires, accented
    bool onScreen = false;
    bool fromBus = false;   // dbus origin -> NotificationClosed is emitted
    u64 postedMs = 0;
};

// what a producer hands to post(); replacesId 0 = new
struct Post {
    stl::StringView app;
    stl::StringView summary;
    stl::StringView body;
    stl::StringView icon;
    bool critical = false;
    bool fromBus = false;
    i32 expireMs = -1; // -1 default, 0 sticky
    u32 replacesId = 0;
};

// fired when a toast leaves the screen; the dbus adapter turns it into the
// spec's NotificationClosed signal
struct NotifierListener {
    virtual void notificationClosed(u32 id, u32 reason) = 0;
};

// the notification store — the single UI-facing model both producers write
// to: the dbus service (org.freedesktop.Notifications) and internal senders
// (wifi, low battery) alike call post(). the renderer draws active(); the
// notification-history view reads history()
struct Notifier {
    virtual u32 post(const Post& p) = 0;
    virtual void close(u32 id, u32 reason) = 0; // reason per the fdo spec
    virtual void dismiss(u32 id) = 0;           // reason 2 (dismissed)

    virtual void activeImpl(stl::VisitorFace&& vis) = 0;  // on-screen, oldest first
    virtual void historyImpl(stl::VisitorFace&& vis) = 0; // all kept, newest first
    virtual void clearHistory() = 0;

    // do-not-disturb: posts still land in history, but never pop on screen
    virtual bool dnd() = 0;
    virtual void setDnd(bool v) = 0;

    virtual void setListener(NotifierListener* l) = 0;

    template <typename F>
    void active(F f) {
        activeImpl(visitEach<Toast>(f));
    }

    template <typename F>
    void history(F f) {
        historyImpl(visitEach<Toast>(f));
    }

    static Notifier* create(Composer& c);
};
