// DRM/KMS-бэкенд: atomic modeset + dumb-буферы, кадр рендерера копируется в scanout.
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
