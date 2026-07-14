#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Output {
    virtual ~Output() noexcept;

    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double refresh() const = 0;

    virtual bool start() = 0;
    virtual void present(const void* pixels) = 0;

    static Output* createKms(stl::ObjPool* pool, struct ev_loop* loop, const char* devPath);

    static Output* createHeadless(stl::ObjPool* pool, int width, int height, double refresh);
};
