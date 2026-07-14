#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct InputSink;
struct Session;
struct Scene;

struct InputSource {
    static InputSource* createLibinput(stl::ObjPool* pool, struct ev_loop* loop, Session& session, InputSink& sink, Scene& scene);
};
