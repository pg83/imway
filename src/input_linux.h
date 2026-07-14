// libinput → Seat: мышь (абсолютная/относительная), кнопки, колесо, клавиатура.
#pragma once

namespace stl {
    class ObjPool;
}

struct Server;

struct InputLinux {
    virtual ~InputLinux() noexcept;

    // бросает stl::Exception, если libinput/udev не поднялись
    static InputLinux* create(stl::ObjPool* pool, Server&);
};
