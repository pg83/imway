// Control-канал: отладочный харнесс. FIFO с текстовыми командами — инъекция
// ввода (через InputSink), скриншот (через Renderer), quit (через ev loop).
#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct InputSink;
struct Renderer;

struct Control {
    virtual ~Control() noexcept;

    // бросает stl::Exception, если FIFO не создался
    static Control* create(stl::ObjPool* pool, struct ev_loop* loop, InputSink& sink,
                           Renderer& renderer, const char* fifoPath);
};
