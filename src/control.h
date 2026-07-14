// Control-канал: FIFO с текстовыми командами для инъекции input и управления.
#pragma once

namespace stl {
    class ObjPool;
}

struct Server;

struct Control {
    virtual ~Control() noexcept;

    // бросает stl::Exception, если FIFO не создался
    static Control* create(stl::ObjPool* pool, Server&, const char* fifoPath);
};
