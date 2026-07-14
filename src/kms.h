// Линуксовые бэкенды: DRM/KMS-вывод, libinput-ввод, control-FIFO.
#pragma once

namespace stl {
    class ObjPool;
}

struct Server;

struct Kms {
    virtual ~Kms() noexcept;

    // modeset + первый кадр; звать после инициализации рендерера
    virtual bool start() = 0;
    virtual void present(const void* pixels) = 0;

    // открывает DRM, выбирает коннектор/режим; выставляет server.outW/outH;
    // бросает stl::Exception, если девайс не открылся или нет atomic
    static Kms* create(stl::ObjPool* pool, Server&, const char* devPath);
};

struct InputLinux {
    virtual ~InputLinux() noexcept;

    // бросает stl::Exception, если libinput/udev не поднялись
    static InputLinux* create(stl::ObjPool* pool, Server&);
};

// control-канал: FIFO с текстовыми командами для инъекции input и управления
struct Control {
    virtual ~Control() noexcept;

    static Control* create(stl::ObjPool* pool, Server&, const char* fifoPath);
};
