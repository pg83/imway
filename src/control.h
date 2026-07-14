// Control-канал: FIFO с текстовыми командами для инъекции input и управления.
#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct Renderer;
struct Seat;

struct Control {
    virtual ~Control() noexcept;

    // бросает stl::Exception, если FIFO не создался
    static Control* create(stl::ObjPool* pool, struct ev_loop* loop, Seat& seat,
                           Renderer& renderer, const char* fifoPath);
};
