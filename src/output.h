// Output — куда показывать кадры. Реализации: DRM/KMS (atomic modeset +
// dumb-буферы) и headless (в никуда, для тестов).
#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Output {
    virtual ~Output() noexcept;

    // режим дисплея — вызывающий подгоняет под него сцену
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double refresh() const = 0;

    // modeset + первый кадр (для headless — no-op)
    virtual bool start() = 0;
    virtual void present(const void* pixels) = 0;

    // открывает DRM, выбирает коннектор/режим;
    // бросает stl::Exception, если девайс не открылся или нет atomic
    static Output* createKms(stl::ObjPool* pool, struct ev_loop* loop, const char* devPath);

    static Output* createHeadless(stl::ObjPool* pool, int width, int height, double refresh);
};
