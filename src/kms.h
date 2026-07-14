// DRM/KMS-бэкенд: atomic modeset + dumb-буферы, кадр рендерера копируется в scanout.
#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Kms {
    virtual ~Kms() noexcept;

    // режим выбранного дисплея — вызывающий подгоняет под него output
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double refresh() const = 0;

    // modeset + первый кадр; звать после инициализации рендерера
    virtual bool start() = 0;
    virtual void present(const void* pixels) = 0;

    // открывает DRM, выбирает коннектор/режим;
    // бросает stl::Exception, если девайс не открылся или нет atomic
    static Kms* create(stl::ObjPool* pool, struct ev_loop* loop, const char* devPath);
};
