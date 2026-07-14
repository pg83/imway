// Seat: клавиатура + указатель, фокус, маршрутизация к клиентам.
#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Popup;
struct Server;
struct Surface;
struct Toplevel;

struct Seat {
    virtual ~Seat() noexcept;

    // события от бэкенда (инъекция/libinput)
    virtual void handleMotion(double x, double y) = 0;
    virtual void handleButton(u32 button, bool pressed) = 0;
    virtual void handleKey(u32 evdevCode, bool pressed) = 0;
    virtual void handleScroll(double value) = 0; // в делениях колеса

    // жизненный цикл окон
    virtual void focusToplevel(Toplevel*) = 0; // клавиатурный фокус
    virtual void toplevelGone(Toplevel*) = 0;
    virtual void surfaceGone(Surface*) = 0;
    virtual void popupGrabStart(Popup*) = 0; // клавиатура → попапу на время grab'а
    virtual void popupGone(Popup*) = 0;

    // бросает stl::Exception, если xkb не поднялся
    static Seat* create(stl::ObjPool* pool, Server&);
};

void seatCreateGlobal(Server&);
