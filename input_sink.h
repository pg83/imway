#pragma once

#include "weak_ptr.h"

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
    // high-resolution wheel steps (multiples of 120, one notch = 120), wheel
    // source only; zero means "derive from discrete"
    i32 value120X = 0;
    i32 value120Y = 0;
    ScrollSource source = ScrollSource::continuous;
    bool stopX = false;
    bool stopY = false;
};

// tablet-v2: one atomic frame of a stylus/tool. Absolute x/y are in output
// pixels; only the *Set axes carry a value this frame. A single frame may
// combine a phase change (proximity/tip) with axis motion
enum class TabletPhase : u8 {
    proximityIn,
    proximityOut,
    tipDown,
    tipUp,
    motion,
};

struct TabletToolEvent {
    TabletPhase phase = TabletPhase::motion;
    u32 toolType = 0x140;   // wp_tablet_tool_v2 type: pen
    double x = 0, y = 0;

    bool pressureSet = false;
    double pressure = 0;    // 0..1
    bool distanceSet = false;
    double distance = 0;    // 0..1
    bool tiltSet = false;
    double tiltX = 0, tiltY = 0;  // degrees
    bool rotationSet = false;
    double rotation = 0;    // degrees
    bool sliderSet = false;
    double slider = 0;      // -1..1
    bool wheelSet = false;
    double wheelDegrees = 0;
    i32 wheelClicks = 0;

    bool buttonSet = false;
    u32 button = 0;
    bool buttonPressed = false;
};

struct InputSink: stl::IntrusiveNode {
    // self-seated weak-ring anchor, invalidated by the destructor: gesture
    // owners in the router null themselves when a sink dies mid-gesture
    Weak<InputSink> weak;

    InputSink() noexcept;

    virtual bool pointerMotion(PointerMotionEvent& ev) = 0;
    virtual bool button(u32 evdevBtn, bool pressed) = 0;
    virtual bool key(u32 evdevCode, bool pressed) = 0;
    virtual bool scroll(const ScrollEvent& ev) = 0;
    virtual bool tabletTool(const TabletToolEvent& ev) = 0;

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
