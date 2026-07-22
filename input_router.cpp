#include "input_router.h"

#include "composer.h"
#include "input_sink.h"
#include "intr_list.h"

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

bool InputRouter::pointerMotion(PointerMotionEvent& ev) {
    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->pointerMotion(ev)) {
            return true;
        }
    }

    return false;
}

bool InputRouter::button(u32 evdevBtn, bool pressed) {
    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->button(evdevBtn, pressed)) {
            return true;
        }
    }

    return false;
}

bool InputRouter::key(u32 evdevCode, bool pressed) {
    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->key(evdevCode, pressed)) {
            return true;
        }
    }

    return false;
}

bool InputRouter::scroll(const ScrollEvent& ev) {
    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->scroll(ev)) {
            return true;
        }
    }

    return false;
}

bool InputRouter::tabletTool(const TabletToolEvent& ev) {
    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->tabletTool(ev)) {
            return true;
        }
    }

    return false;
}

bool InputRouter::swipeBegin(u32 fingers) {
    if (InputSink* owner = swipeOwner.get()) {
        owner->swipeEnd(true);
    }

    swipeOwner.reset();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->swipeBegin(fingers)) {
            swipeOwner.bind(sink->weak);

            return true;
        }
    }

    return false;
}

bool InputRouter::swipeUpdate(double dx, double dy) {
    if (!swipeOwner.get()) {
        swipeOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == swipeOwner.get()) {
            sink->swipeUpdate(dx, dy);

            return true;
        }

        if (sink->swipeUpdate(dx, dy)) {
            InputSink* previous = swipeOwner.get();

            swipeOwner.reset();
            previous->swipeEnd(true);

            return true;
        }
    }

    return false;
}

bool InputRouter::swipeEnd(bool cancelled) {
    if (!swipeOwner.get()) {
        swipeOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == swipeOwner.get()) {
            swipeOwner.reset();
            sink->swipeEnd(cancelled);

            return true;
        }

        if (sink->swipeEnd(cancelled)) {
            InputSink* previous = swipeOwner.get();

            swipeOwner.reset();
            previous->swipeEnd(true);

            return true;
        }
    }

    return false;
}

bool InputRouter::pinchBegin(u32 fingers) {
    if (InputSink* owner = pinchOwner.get()) {
        owner->pinchEnd(true);
    }

    pinchOwner.reset();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->pinchBegin(fingers)) {
            pinchOwner.bind(sink->weak);

            return true;
        }
    }

    return false;
}

bool InputRouter::pinchUpdate(double dx, double dy, double scale, double rotation) {
    if (!pinchOwner.get()) {
        pinchOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == pinchOwner.get()) {
            sink->pinchUpdate(dx, dy, scale, rotation);

            return true;
        }

        if (sink->pinchUpdate(dx, dy, scale, rotation)) {
            InputSink* previous = pinchOwner.get();

            pinchOwner.reset();
            previous->pinchEnd(true);

            return true;
        }
    }

    return false;
}

bool InputRouter::pinchEnd(bool cancelled) {
    if (!pinchOwner.get()) {
        pinchOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == pinchOwner.get()) {
            pinchOwner.reset();
            sink->pinchEnd(cancelled);

            return true;
        }

        if (sink->pinchEnd(cancelled)) {
            InputSink* previous = pinchOwner.get();

            pinchOwner.reset();
            previous->pinchEnd(true);

            return true;
        }
    }

    return false;
}

bool InputRouter::holdBegin(u32 fingers) {
    if (InputSink* owner = holdOwner.get()) {
        owner->holdEnd(true);
    }

    holdOwner.reset();

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->holdBegin(fingers)) {
            holdOwner.bind(sink->weak);

            return true;
        }
    }

    return false;
}

bool InputRouter::holdEnd(bool cancelled) {
    if (!holdOwner.get()) {
        holdOwner.reset();

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == holdOwner.get()) {
            holdOwner.reset();
            sink->holdEnd(cancelled);

            return true;
        }

        if (sink->holdEnd(cancelled)) {
            InputSink* previous = holdOwner.get();

            holdOwner.reset();
            previous->holdEnd(true);

            return true;
        }
    }

    return false;
}

InputSink* createInputRouter(Composer& c) {
    return c.pool->make<InputRouter>(c);
}
