#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct InputSink;
struct Renderer;

struct Control {

    static Control* create(stl::ObjPool* pool, struct ev_loop* loop, InputSink& sink, Renderer& renderer, const char* fifoPath);
};
