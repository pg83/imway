#pragma once

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Renderer;
struct InputSink;

struct Control {
    static Control* create(Composer& c, stl::StringView fifoPath);
};
