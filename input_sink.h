#pragma once

#include <std/lib/node.h>
#include <std/sys/types.h>

enum class ScrollSource : u8 {
    wheel,
    finger,
    continuous,
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
    virtual void motion(double x, double y) = 0;
    virtual void relMotion(double dx, double dy, double dxRaw, double dyRaw) = 0;

    // absolute device position normalized to [0..1]; whoever owns the
    // cursor maps it to the screen
    virtual void absMotion(double nx, double ny) = 0;
    virtual void button(u32 evdevBtn, bool pressed) = 0;
    virtual void key(u32 evdevCode, bool pressed) = 0;
    virtual void modsChanged() = 0;
    virtual void scroll(const ScrollEvent& ev) = 0;

    virtual void swipeBegin(u32 fingers) = 0;
    virtual void swipeUpdate(double dx, double dy) = 0;
    virtual void swipeEnd(bool cancelled) = 0;
    virtual void pinchBegin(u32 fingers) = 0;
    virtual void pinchUpdate(double dx, double dy, double scale, double rotation) = 0;
    virtual void pinchEnd(bool cancelled) = 0;
    virtual void holdBegin(u32 fingers) = 0;
    virtual void holdEnd(bool cancelled) = 0;

    virtual ~InputSink() noexcept;
};
