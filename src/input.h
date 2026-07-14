#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct InputSink;

struct InputSource {
    static InputSource* createLibinput(stl::ObjPool* pool, struct ev_loop* loop, InputSink& sink, int outW, int outH);
};
