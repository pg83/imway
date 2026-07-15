#pragma once

#include <std/str/view.h>

struct DirVisitorFace {
    virtual void visit(stl::StringView dir) = 0;
};

// libstd visitor.h style: the wrapper adapts any functor to the face, so
// call sites pass a lambda straight in
template <typename F>
struct DirVisitor: public DirVisitorFace {
    F fn;

    DirVisitor(F f) : fn(f) {
    }

    void visit(stl::StringView dir) override {
        fn(dir);
    }
};

// XDG_DATA_HOME (or ~/.local/share) followed by XDG_DATA_DIRS
void forEachXdgDataDir(DirVisitorFace& v);

template <typename F>
void forEachXdgDataDir(F f) {
    DirVisitor<F> v(f);

    // explicit face ref: the unqualified call would pick this template again
    forEachXdgDataDir((DirVisitorFace&)v);
}
