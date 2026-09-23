#include "input_router.h"

#include "composer.h"
#include "listener.h"
#include "intr_list.h"
#include "input_sink.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct InputRouter: InputSink {
        Composer* comp = nullptr;
        // weak: a sink dying mid-gesture drops ownership through the ring
        // instead of a list-membership probe per event
        Weak<InputSink> swipeOwner;
        Weak<InputSink> pinchOwner;
        Weak<InputSink> holdOwner;

        InputRouter(Composer& c);

        void activity();
        bool pointerMotion(PointerMotionEvent& ev) override;
        bool button(u32 evdevBtn, bool pressed) override;
        bool key(u32 evdevCode, bool pressed) override;
        bool scroll(const ScrollEvent& ev) override;
        bool tabletTool(const TabletToolEvent& ev) override;
        bool swipeBegin(u32 fingers) override;
        bool swipeUpdate(double dx, double dy) override;
        bool swipeEnd(bool cancelled) override;
        bool pinchBegin(u32 fingers) override;
        bool pinchUpdate(double dx, double dy, double scale, double rotation) override;
        bool pinchEnd(bool cancelled) override;
        bool holdBegin(u32 fingers) override;
        bool holdEnd(bool cancelled) override;
    };
}

namespace {
    // the first sink to claim an event; the walk needs no end test, as the
    // wayland seat, last in the list, claims every one (see activity())
    template <typename F>
    static InputSink* claimant(Composer* comp, F claims) {
        IntrusiveNode* n = comp->inputSinks.mutFront();

        while (!claims((InputSink*)n)) {
            n = n->next;
        }

        return (InputSink*)n;
    }
}

InputRouter::InputRouter(Composer& c)
    : comp(&c)
{
}

// Raw input activity is announced before any sink sees the event, so a sink
// which consumes it (notably the lock screen) cannot hide it from DPMS wake
// or the idle lock.
void InputRouter::activity() {
    forEach<Listener>(comp->inputActivityListeners, [](Listener& listener) {
        listener.onListen();
    });
}

bool InputRouter::pointerMotion(PointerMotionEvent& ev) {
    activity();
    claimant(comp, [&](InputSink* sink) {
        return sink->pointerMotion(ev);
    });

    return true;
}

bool InputRouter::button(u32 evdevBtn, bool pressed) {
    activity();
    claimant(comp, [&](InputSink* sink) {
        return sink->button(evdevBtn, pressed);
    });

    return true;
}

bool InputRouter::key(u32 evdevCode, bool pressed) {
    activity();
    claimant(comp, [&](InputSink* sink) {
        return sink->key(evdevCode, pressed);
    });

    return true;
}

bool InputRouter::scroll(const ScrollEvent& ev) {
    activity();
    claimant(comp, [&](InputSink* sink) {
        return sink->scroll(ev);
    });

    return true;
}

bool InputRouter::tabletTool(const TabletToolEvent& ev) {
    activity();
    claimant(comp, [&](InputSink* sink) {
        return sink->tabletTool(ev);
    });

    return true;
}

bool InputRouter::swipeBegin(u32 fingers) {
    activity();

    if (InputSink* owner = swipeOwner.get()) {
        owner->swipeEnd(true);
    }

    swipeOwner.reset();
    swipeOwner.bind(claimant(comp, [&](InputSink* sink) {
        return sink->swipeBegin(fingers);
    })->weak);

    return true;
}

bool InputRouter::swipeUpdate(double dx, double dy) {
    activity();

    InputSink* owner = swipeOwner.get();

    if (!owner) {
        swipeOwner.reset();

        return false;
    }

    // the owner is still in the list (a dead one nulls the weak), so the
    // walk stops at it at the latest
    InputSink* sink = claimant(comp, [&](InputSink* s) {
        return s == owner || s->swipeUpdate(dx, dy);
    });

    if (sink == owner) {
        sink->swipeUpdate(dx, dy);
    } else {
        swipeOwner.reset();
        owner->swipeEnd(true);
    }

    return true;
}

bool InputRouter::swipeEnd(bool cancelled) {
    activity();

    InputSink* owner = swipeOwner.get();

    if (!owner) {
        swipeOwner.reset();

        return false;
    }

    InputSink* sink = claimant(comp, [&](InputSink* s) {
        return s == owner || s->swipeEnd(cancelled);
    });

    swipeOwner.reset();

    if (sink == owner) {
        sink->swipeEnd(cancelled);
    } else {
        owner->swipeEnd(true);
    }

    return true;
}

bool InputRouter::pinchBegin(u32 fingers) {
    activity();

    if (InputSink* owner = pinchOwner.get()) {
        owner->pinchEnd(true);
    }

    pinchOwner.reset();
    pinchOwner.bind(claimant(comp, [&](InputSink* sink) {
        return sink->pinchBegin(fingers);
    })->weak);

    return true;
}

bool InputRouter::pinchUpdate(double dx, double dy, double scale, double rotation) {
    activity();

    InputSink* owner = pinchOwner.get();

    if (!owner) {
        pinchOwner.reset();

        return false;
    }

    // the owner is still in the list (a dead one nulls the weak), so the
    // walk stops at it at the latest
    InputSink* sink = claimant(comp, [&](InputSink* s) {
        return s == owner || s->pinchUpdate(dx, dy, scale, rotation);
    });

    if (sink == owner) {
        sink->pinchUpdate(dx, dy, scale, rotation);
    } else {
        pinchOwner.reset();
        owner->pinchEnd(true);
    }

    return true;
}

bool InputRouter::pinchEnd(bool cancelled) {
    activity();

    InputSink* owner = pinchOwner.get();

    if (!owner) {
        pinchOwner.reset();

        return false;
    }

    InputSink* sink = claimant(comp, [&](InputSink* s) {
        return s == owner || s->pinchEnd(cancelled);
    });

    pinchOwner.reset();

    if (sink == owner) {
        sink->pinchEnd(cancelled);
    } else {
        owner->pinchEnd(true);
    }

    return true;
}

bool InputRouter::holdBegin(u32 fingers) {
    activity();

    if (InputSink* owner = holdOwner.get()) {
        owner->holdEnd(true);
    }

    holdOwner.reset();
    holdOwner.bind(claimant(comp, [&](InputSink* sink) {
        return sink->holdBegin(fingers);
    })->weak);

    return true;
}

bool InputRouter::holdEnd(bool cancelled) {
    activity();

    InputSink* owner = holdOwner.get();

    if (!owner) {
        holdOwner.reset();

        return false;
    }

    InputSink* sink = claimant(comp, [&](InputSink* s) {
        return s == owner || s->holdEnd(cancelled);
    });

    holdOwner.reset();

    if (sink == owner) {
        sink->holdEnd(cancelled);
    } else {
        owner->holdEnd(true);
    }

    return true;
}

InputSink* createInputRouter(Composer& c) {
    return c.pool->make<InputRouter>(c);
}
