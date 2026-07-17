#pragma once

#include <std/mem/obj_pool.h>
#include <std/ptr/refcount.h>

// A dialog is the root object of its own arena.  The opaque Dialog* kept by
// the caller represents the arena's sole external reference; Dialog itself
// only keeps the raw pointer needed to retire that reference without forming
// a cycle.
template <typename T, typename... A>
T* dialogPoolCreate(A&&... args) {
    stl::ObjPool* pool = stl::ObjPool::fromMemoryRaw();

    stl::RefCountOps<stl::ObjPool>::ref(pool);

    try {
        return pool->make<T>(pool, stl::forward<A>(args)...);
    } catch (...) {
        stl::RefCountOps<stl::ObjPool>::unref(pool);
        throw;
    }
}

template <typename T>
void dialogPoolDestroy(T*& dialog) noexcept {
    if (!dialog) {
        return;
    }

    stl::ObjPool* pool = dialog->pool;

    // The pool owns dialog, so the handle must stop exposing it before the
    // final unref runs Dialog::~Dialog and releases the arena.
    dialog = nullptr;
    stl::RefCountOps<stl::ObjPool>::unref(pool);
}
