#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct InputSink;
struct Session;

struct InputSource {
    static InputSource* createLibinput(Composer& c);
};
