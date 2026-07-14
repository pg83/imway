// Ввод: источники сырых событий (libinput, инъекция) и синк-потребитель.
// Источники не знают, кто потребляет: синки — это view (ImGui) и wayland-SM.
#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct InputSink {
    virtual void motion(double x, double y) = 0; // абсолютные координаты output
    virtual void button(u32 evdevBtn, bool pressed) = 0;
    virtual void key(u32 evdevCode, bool pressed) = 0;
    virtual void scroll(double value) = 0; // в делениях колеса

    // размножить события на два синка (view + протокол)
    static InputSink* tee(stl::ObjPool* pool, InputSink& a, InputSink& b);
};

struct InputSource {
    virtual ~InputSource() noexcept;

    // libinput/udev; outW/outH — границы относительного курсора и масштаб
    // абсолютного; бросает stl::Exception, если udev/libinput не поднялись
    static InputSource* createLibinput(stl::ObjPool* pool, struct ev_loop* loop, InputSink& sink,
                                       int outW, int outH);
};
