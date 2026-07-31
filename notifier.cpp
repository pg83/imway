#include "notifier.h"

#include "util.h"
#include "scene.h"
#include "composer.h"
#include "listener.h"
#include "intr_list.h"

#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/mem/small_obj_allocator.h>

#include <ev.h>
#include <time.h>

using namespace stl;

namespace {
    constexpr u32 kExpired = 1;
    constexpr u32 kDismissed = 2;

    struct NotifierImpl;

    struct ToastImpl: public Toast {
        NotifierImpl* store = nullptr;
        ev_timer timer{};
    };

    void expiryCb(struct ev_loop*, ev_timer* w, int);
    void scheduleCb(struct ev_loop*, ev_timer* w, int);

    struct NotifierImpl: public Notifier {
        Composer* c = nullptr;
        struct ev_loop* loop = nullptr;
        IntrusiveList toasts; // newest last
        u32 lastId = 0;
        ev_timer scheduleTimer{};
        NotifierImpl(Composer& comp);
        ~NotifierImpl() noexcept;

        u32 post(const Post& p) override;
        void close(u32 id, u32 reason) override;
        void dismiss(u32 id) override;
        void activeImpl(VisitorFace&& vis) override;
        void historyImpl(VisitorFace&& vis) override;
        void clearHistory() override;
        bool dnd() override;
        void setDnd(bool v) override;

        ToastImpl* byId(u32 id);
        void armTimer(ToastImpl& t, i32 expireMs);
        void trim();
        bool dndActive() const;
        void applyDnd();
    };

    struct CallNotifierSetting: Listener {
        NotifierImpl* parent;
        void (NotifierImpl::*callback)();

        CallNotifierSetting(NotifierImpl* p, void (NotifierImpl::*cb)());
        void onListen(void*) override;
    };
}

NotifierImpl::NotifierImpl(Composer& comp)
    : c(&comp)
    , loop(comp.loop)
{
    comp.settings->addDndListener(comp.pool->make<CallNotifierSetting>(this, &NotifierImpl::applyDnd));
    comp.settings->addDndScheduledListener(comp.pool->make<CallNotifierSetting>(this, &NotifierImpl::applyDnd));
    comp.settings->addDndStartMinuteListener(comp.pool->make<CallNotifierSetting>(this, &NotifierImpl::applyDnd));
    comp.settings->addDndEndMinuteListener(comp.pool->make<CallNotifierSetting>(this, &NotifierImpl::applyDnd));
    comp.settings->addNotificationHistoryListener(comp.pool->make<CallNotifierSetting>(this, &NotifierImpl::trim));

    ev_timer_init(&scheduleTimer, scheduleCb, 30., 30.);
    scheduleTimer.data = this;
    ev_timer_start(loop, &scheduleTimer);
}

NotifierImpl::~NotifierImpl() noexcept {
    ev_timer_stop(loop, &scheduleTimer);

    while (!toasts.empty()) {
        auto* t = (ToastImpl*)(Toast*)toasts.popFront();

        ev_timer_stop(loop, &t->timer);
        c->alloc->release(t);
    }
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

    double sec = expireMs > 0 ? expireMs / 1000.0 : c->settings->notificationSeconds();

    ev_timer_init(&t.timer, expiryCb, sec, 0.);
    t.timer.data = &t;
    ev_timer_start(loop, &t.timer);
}

// keep history bounded: drop the oldest off-screen toasts past the cap
void NotifierImpl::trim() {
    size_t limit = (size_t)c->settings->notificationHistory();

    while (toasts.length() > limit) {
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
        c->alloc->release((ToastImpl*)victim);
    }
}

u32 NotifierImpl::post(const Post& p) {
    ToastImpl* t = p.replacesId ? byId(p.replacesId) : nullptr;

    if (!t) {
        t = c->alloc->make<ToastImpl>();
        t->store = this;
        t->id = ++lastId;
        toasts.pushBack(t);
    }

    t->app.reset();
    t->app.append(p.app.data(), p.app.length());
    t->summary.reset();
    t->summary.append(p.summary.data(), p.summary.length());
    t->body.reset();
    t->body.append(p.body.data(), p.body.length());
    t->icon.reset();
    t->icon.append(p.icon.data(), p.icon.length());
    t->critical = p.critical && c->settings->allowCriticalNotifications();
    t->fromBus = p.fromBus;
    t->postedMs = nowMsec();
    NotificationPolicy policy = NotificationPolicy::defaultPolicy;
    size_t rules = c->settings->notificationRuleCount();

    for (size_t i = 0; i < rules && i < Settings::notificationRuleCapacity; i++) {
        const NotificationRule& rule = c->settings->notificationRule(i);

        if (StringView(rule.app) == p.app) {
            policy = rule.policy;
            break;
        }
    }

    t->onScreen = policy == NotificationPolicy::allow || (policy == NotificationPolicy::defaultPolicy && !dndActive());

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

    if (t->fromBus) {
        NotificationClosedEvent event{id, reason};

        forEach<Listener>(c->notifierListeners, [&event](Listener& listener) {
            listener.onListen(&event);
        });
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
            c->alloc->release((ToastImpl*)t);
        }
    }

    c->scene->needsFrame = true;
}

bool NotifierImpl::dnd() {
    return dndActive();
}

void NotifierImpl::setDnd(bool v) {
    c->settings->setDnd(v);
}

bool NotifierImpl::dndActive() const {
    if (c->settings->dnd()) {
        return true;
    }

    if (!c->settings->dndScheduled()) {
        return false;
    }

    time_t now = time(nullptr);
    tm local{};

    localtime_r(&now, &local);

    int minute = local.tm_hour * 60 + local.tm_min;
    int start = c->settings->dndStartMinute();
    int end = c->settings->dndEndMinute();

    return start <= end ? minute >= start && minute < end : minute >= start || minute < end;
}

void NotifierImpl::applyDnd() {
    if (dndActive()) {
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

CallNotifierSetting::CallNotifierSetting(NotifierImpl* p, void (NotifierImpl::*cb)())
    : parent(p)
    , callback(cb)
{
}

void CallNotifierSetting::onListen(void*) {
    (parent->*callback)();
    parent->c->scene->needsFrame = true;
}

namespace {
    void expiryCb(struct ev_loop*, ev_timer* w, int) {
        auto* t = (ToastImpl*)w->data;

        t->store->close(t->id, kExpired);
    }

    void scheduleCb(struct ev_loop*, ev_timer* w, int) {
        ((NotifierImpl*)w->data)->applyDnd();
    }
}

Notifier* Notifier::create(Composer& c) {
    return c.pool->make<NotifierImpl>(c);
}
