#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct InputSink {
    virtual void motion(double x, double y) = 0;
    virtual void button(u32 evdevBtn, bool pressed) = 0;
    virtual void key(u32 evdevCode, bool pressed) = 0;
    virtual void scroll(double value) = 0;

    static InputSink* tee(stl::ObjPool* pool, InputSink& a, InputSink& b);
};

struct InputSource {

    static InputSource* createLibinput(stl::ObjPool* pool, struct ev_loop* loop, InputSink& sink, int outW, int outH);
};
