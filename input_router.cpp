#include "input_router.h"

#include "wayland.h"
#include "composer.h"
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

InputRouter::InputRouter(Composer& c)
    : comp(&c)
{
}

// The wayland seat is made before any input source and outlives them all
// (the pool unwinds in reverse), sits last in the sink list and claims every
// event: a walk always ends at a claiming sink, and the router claims too.
void InputRouter::activity() {
    comp->wayland->inputActivity();
}

bool InputRouter::pointerMotion(PointerMotionEvent& ev) {
    activity();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->pointerMotion(ev)) {
            break;
        }
    }

    return true;
}

bool InputRouter::button(u32 evdevBtn, bool pressed) {
    activity();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->button(evdevBtn, pressed)) {
            break;
        }
    }

    return true;
}

bool InputRouter::key(u32 evdevCode, bool pressed) {
    activity();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->key(evdevCode, pressed)) {
            break;
        }
    }

    return true;
}

bool InputRouter::scroll(const ScrollEvent& ev) {
    activity();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->scroll(ev)) {
            break;
        }
    }

    return true;
}

bool InputRouter::tabletTool(const TabletToolEvent& ev) {
    activity();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->tabletTool(ev)) {
            break;
        }
    }

    return true;
}

bool InputRouter::swipeBegin(u32 fingers) {
    activity();

    if (InputSink* owner = swipeOwner.get()) {
        owner->swipeEnd(true);
    }

    swipeOwner.reset();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->swipeBegin(fingers)) {
            swipeOwner.bind(sink->weak);

            break;
        }
    }

    return true;
}

bool InputRouter::swipeUpdate(double dx, double dy) {
    activity();

    if (!swipeOwner.get()) {
        swipeOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == swipeOwner.get()) {
            sink->swipeUpdate(dx, dy);

            break;
        }

        if (sink->swipeUpdate(dx, dy)) {
            InputSink* previous = swipeOwner.get();

            swipeOwner.reset();
            previous->swipeEnd(true);

            break;
        }
    }

    return true;
}

bool InputRouter::swipeEnd(bool cancelled) {
    activity();

    if (!swipeOwner.get()) {
        swipeOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == swipeOwner.get()) {
            swipeOwner.reset();
            sink->swipeEnd(cancelled);

            break;
        }

        if (sink->swipeEnd(cancelled)) {
            InputSink* previous = swipeOwner.get();

            swipeOwner.reset();
            previous->swipeEnd(true);

            break;
        }
    }

    return true;
}

bool InputRouter::pinchBegin(u32 fingers) {
    activity();

    if (InputSink* owner = pinchOwner.get()) {
        owner->pinchEnd(true);
    }

    pinchOwner.reset();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->pinchBegin(fingers)) {
            pinchOwner.bind(sink->weak);

            break;
        }
    }

    return true;
}

bool InputRouter::pinchUpdate(double dx, double dy, double scale, double rotation) {
    activity();

    if (!pinchOwner.get()) {
        pinchOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == pinchOwner.get()) {
            sink->pinchUpdate(dx, dy, scale, rotation);

            break;
        }

        if (sink->pinchUpdate(dx, dy, scale, rotation)) {
            InputSink* previous = pinchOwner.get();

            pinchOwner.reset();
            previous->pinchEnd(true);

            break;
        }
    }

    return true;
}

bool InputRouter::pinchEnd(bool cancelled) {
    activity();

    if (!pinchOwner.get()) {
        pinchOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == pinchOwner.get()) {
            pinchOwner.reset();
            sink->pinchEnd(cancelled);

            break;
        }

        if (sink->pinchEnd(cancelled)) {
            InputSink* previous = pinchOwner.get();

            pinchOwner.reset();
            previous->pinchEnd(true);

            break;
        }
    }

    return true;
}

bool InputRouter::holdBegin(u32 fingers) {
    activity();

    if (InputSink* owner = holdOwner.get()) {
        owner->holdEnd(true);
    }

    holdOwner.reset();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->holdBegin(fingers)) {
            holdOwner.bind(sink->weak);

            break;
        }
    }

    return true;
}

bool InputRouter::holdEnd(bool cancelled) {
    activity();

    if (!holdOwner.get()) {
        holdOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == holdOwner.get()) {
            holdOwner.reset();
            sink->holdEnd(cancelled);

            break;
        }

        if (sink->holdEnd(cancelled)) {
            InputSink* previous = holdOwner.get();

            holdOwner.reset();
            previous->holdEnd(true);

            break;
        }
    }

    return true;
}

InputSink* createInputRouter(Composer& c) {
    return c.pool->make<InputRouter>(c);
}
