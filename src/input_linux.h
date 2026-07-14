// libinput → Seat: мышь (абсолютная/относительная), кнопки, колесо, клавиатура.
#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct Seat;

struct InputLinux {
    virtual ~InputLinux() noexcept;

    // outW/outH — границы для относительного курсора и масштаб абсолютного;
    // бросает stl::Exception, если libinput/udev не поднялись
    static InputLinux* create(stl::ObjPool* pool, struct ev_loop* loop, Seat& seat, int outW,
                              int outH);
};
