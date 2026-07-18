#include "composer.h"
#include "intr_list.h"
#include "notifier.h"
#include "scene.h"
#include "util.h"

#include <ev.h>

#include <std/ios/sys.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr double kDefaultExpiry = 5.0;
    constexpr size_t kHistoryMax = 50;

    constexpr u32 kExpired = 1;
    constexpr u32 kDismissed = 2;

    struct NotifierImpl;

    struct ToastImpl: public Toast {
        NotifierImpl* store = nullptr;
        ev_timer timer{};
    };

    void expiryCb(struct ev_loop*, ev_timer* w, int);

    struct NotifierImpl: public Notifier {
        Composer* c = nullptr;
        struct ev_loop* loop = nullptr;
        ObjList<ToastImpl> alloc;
        IntrusiveList toasts; // newest last
        u32 lastId = 0;
        bool dndOn = false;
        NotifierListener* listener = nullptr;

        NotifierImpl(Composer& comp);

        u32 post(const Post& p) override;
        void close(u32 id, u32 reason) override;
        void dismiss(u32 id) override;
        void activeImpl(VisitorFace&& vis) override;
        void historyImpl(VisitorFace&& vis) override;
        void clearHistory() override;
        bool dnd() override;
        void setDnd(bool v) override;
        void setListener(NotifierListener* l) override;

        ToastImpl* byId(u32 id);
        void armTimer(ToastImpl& t, i32 expireMs);
        void trim();
    };
}

NotifierImpl::NotifierImpl(Composer& comp)
    : c(&comp)
    , loop(comp.loop)
    , alloc(comp.pool)
{
}

ToastImpl* NotifierImpl::byId(u32 id) {
    for (Toast* t : each<Toast>(toasts)) {
        if (t->id == id) {
            return (ToastImpl*)t;
        }
    }

    return nullptr;
}

void NotifierImpl::armTimer(ToastImpl& t, i32 expireMs) {
    ev_timer_stop(loop, &t.timer);

    if (t.critical || expireMs == 0) {
        return; // sticky
    }

    double sec = expireMs > 0 ? expireMs / 1000.0 : kDefaultExpiry;

    ev_timer_init(&t.timer, expiryCb, sec, 0.);
    t.timer.data = &t;
    ev_timer_start(loop, &t.timer);
}

// keep history bounded: drop the oldest off-screen toasts past the cap
void NotifierImpl::trim() {
    while (toasts.length() > kHistoryMax) {
        Toast* victim = nullptr;

        for (Toast* t : each<Toast>(toasts)) {
            if (!t->onScreen) {
                victim = t;

                break;
            }
        }

        if (!victim) {
            break; // everything left is on screen
        }

        victim->unlink();
        alloc.release((ToastImpl*)victim);
    }
}

u32 NotifierImpl::post(const Post& p) {
    ToastImpl* t = p.replacesId ? byId(p.replacesId) : nullptr;

    if (!t) {
        t = alloc.make();
        t->store = this;
        t->id = ++lastId;
        toasts.pushBack(t);
    }

    t->app.reset();
    t->app << p.app;
    t->summary.reset();
    t->summary << p.summary;
    t->body.reset();
    t->body << p.body;
    t->icon.reset();
    t->icon << p.icon;
    t->critical = p.critical;
    t->fromBus = p.fromBus;
    t->postedMs = nowMsec();
    t->onScreen = !dndOn;

    if (t->onScreen) {
        armTimer(*t, p.expireMs);
    } else {
        ev_timer_stop(loop, &t->timer);
    }

    trim();
    c->scene->needsFrame = true;

    return t->id;
}

void NotifierImpl::close(u32 id, u32 reason) {
    ToastImpl* t = byId(id);

    if (!t || !t->onScreen) {
        return;
    }

    ev_timer_stop(loop, &t->timer);
    t->onScreen = false;

    if (t->fromBus && listener) {
        listener->notificationClosed(id, reason);
    }

    c->scene->needsFrame = true;
}

void NotifierImpl::dismiss(u32 id) {
    close(id, kDismissed);
}

void NotifierImpl::activeImpl(VisitorFace&& vis) {
    forEach<Toast>(toasts, [&](Toast& t) {
        if (t.onScreen) {
            vis.visit(&t);
        }
    });
}

void NotifierImpl::historyImpl(VisitorFace&& vis) {
    // newest first
    forEachRev<Toast>(toasts, [&](Toast& t) {
        vis.visit(&t);
    });
}

void NotifierImpl::clearHistory() {
    for (IntrusiveNode* n = toasts.mutFront(); n != toasts.mutEnd();) {
        auto* t = (Toast*)n;

        n = n->next; // step past before the unlink

        if (!t->onScreen) {
            t->unlink();
            alloc.release((ToastImpl*)t);
        }
    }

    c->scene->needsFrame = true;
}

bool NotifierImpl::dnd() {
    return dndOn;
}

void NotifierImpl::setDnd(bool v) {
    dndOn = v;

    if (v) {
        // pull everything off screen, keep it in history
        forEach<Toast>(toasts, [&](Toast& t) {
            if (t.onScreen) {
                ev_timer_stop(loop, &((ToastImpl&)t).timer);
                t.onScreen = false;
            }
        });
    }

    c->scene->needsFrame = true;
}

void NotifierImpl::setListener(NotifierListener* l) {
    listener = l;
}

namespace {
    void expiryCb(struct ev_loop*, ev_timer* w, int) {
        auto* t = (ToastImpl*)w->data;

        t->store->close(t->id, kExpired);
    }
}

Notifier* Notifier::create(Composer& c) {
    return c.pool->make<NotifierImpl>(c);
}
