#include "input_router.h"

#include "composer.h"
#include "input_sink.h"
#include "intr_list.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct InputRouter: InputSink {
        Composer* comp = nullptr;
        InputSink* swipeOwner = nullptr;
        InputSink* pinchOwner = nullptr;
        InputSink* holdOwner = nullptr;

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
    if (intrListContains<InputSink>(comp->inputSinks, swipeOwner)) {
        swipeOwner->swipeEnd(true);
    }

    swipeOwner = nullptr;

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->swipeBegin(fingers)) {
            swipeOwner = sink;

            return true;
        }
    }

    return false;
}

bool InputRouter::swipeUpdate(double dx, double dy) {
    if (!intrListContains<InputSink>(comp->inputSinks, swipeOwner)) {
        swipeOwner = nullptr;

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == swipeOwner) {
            sink->swipeUpdate(dx, dy);

            return true;
        }

        if (sink->swipeUpdate(dx, dy)) {
            InputSink* previous = swipeOwner;

            swipeOwner = nullptr;
            previous->swipeEnd(true);

            return true;
        }
    }

    return false;
}

bool InputRouter::swipeEnd(bool cancelled) {
    if (!intrListContains<InputSink>(comp->inputSinks, swipeOwner)) {
        swipeOwner = nullptr;

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == swipeOwner) {
            swipeOwner = nullptr;
            sink->swipeEnd(cancelled);

            return true;
        }

        if (sink->swipeEnd(cancelled)) {
            InputSink* previous = swipeOwner;

            swipeOwner = nullptr;
            previous->swipeEnd(true);

            return true;
        }
    }

    return false;
}

bool InputRouter::pinchBegin(u32 fingers) {
    if (intrListContains<InputSink>(comp->inputSinks, pinchOwner)) {
        pinchOwner->pinchEnd(true);
    }

    pinchOwner = nullptr;

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->pinchBegin(fingers)) {
            pinchOwner = sink;

            return true;
        }
    }

    return false;
}

bool InputRouter::pinchUpdate(double dx, double dy, double scale, double rotation) {
    if (!intrListContains<InputSink>(comp->inputSinks, pinchOwner)) {
        pinchOwner = nullptr;

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == pinchOwner) {
            sink->pinchUpdate(dx, dy, scale, rotation);

            return true;
        }

        if (sink->pinchUpdate(dx, dy, scale, rotation)) {
            InputSink* previous = pinchOwner;

            pinchOwner = nullptr;
            previous->pinchEnd(true);

            return true;
        }
    }

    return false;
}

bool InputRouter::pinchEnd(bool cancelled) {
    if (!intrListContains<InputSink>(comp->inputSinks, pinchOwner)) {
        pinchOwner = nullptr;

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == pinchOwner) {
            pinchOwner = nullptr;
            sink->pinchEnd(cancelled);

            return true;
        }

        if (sink->pinchEnd(cancelled)) {
            InputSink* previous = pinchOwner;

            pinchOwner = nullptr;
            previous->pinchEnd(true);

            return true;
        }
    }

    return false;
}

bool InputRouter::holdBegin(u32 fingers) {
    if (intrListContains<InputSink>(comp->inputSinks, holdOwner)) {
        holdOwner->holdEnd(true);
    }

    holdOwner = nullptr;

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink->holdBegin(fingers)) {
            holdOwner = sink;

            return true;
        }
    }

    return false;
}

bool InputRouter::holdEnd(bool cancelled) {
    if (!intrListContains<InputSink>(comp->inputSinks, holdOwner)) {
        holdOwner = nullptr;

        return false;
    }

    for (InputSink* sink : each<InputSink>(comp->inputSinks)) {
        if (sink == holdOwner) {
            holdOwner = nullptr;
            sink->holdEnd(cancelled);

            return true;
        }

        if (sink->holdEnd(cancelled)) {
            InputSink* previous = holdOwner;

            holdOwner = nullptr;
            previous->holdEnd(true);

            return true;
        }
    }

    return false;
}

InputSink* createInputRouter(Composer& c) {
    return c.pool->make<InputRouter>(c);
}
