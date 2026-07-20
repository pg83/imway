#pragma once

#include <std/lib/node.h>
#include <std/sys/types.h>

enum class ScrollSource : u8 {
    wheel,
    finger,
    continuous,
};

enum class PointerMotionKind : u8 {
    position,
    relative,
    absolute,
};

struct PointerMotionEvent {
    PointerMotionKind kind = PointerMotionKind::position;
    double x = 0;
    double y = 0;
    double dx = 0;
    double dy = 0;
    double dxRaw = 0;
    double dyRaw = 0;
    bool moved = false;
};

struct ScrollEvent {
    double dx = 0;
    double dy = 0;
    i32 discreteX = 0;
    i32 discreteY = 0;
    ScrollSource source = ScrollSource::continuous;
    bool stopX = false;
    bool stopY = false;
};

struct InputSink: stl::IntrusiveNode {
    virtual bool pointerMotion(PointerMotionEvent& ev) = 0;
    virtual bool button(u32 evdevBtn, bool pressed) = 0;
    virtual bool key(u32 evdevCode, bool pressed) = 0;
    virtual bool scroll(const ScrollEvent& ev) = 0;

    virtual bool swipeBegin(u32 fingers) = 0;
    virtual bool swipeUpdate(double dx, double dy) = 0;
    virtual bool swipeEnd(bool cancelled) = 0;
    virtual bool pinchBegin(u32 fingers) = 0;
    virtual bool pinchUpdate(double dx, double dy, double scale, double rotation) = 0;
    virtual bool pinchEnd(bool cancelled) = 0;
    virtual bool holdBegin(u32 fingers) = 0;
    virtual bool holdEnd(bool cancelled) = 0;

    virtual ~InputSink() noexcept;
};
