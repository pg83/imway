#pragma once

#include <std/mem/obj_pool.h>
#include <std/ptr/refcount.h>

template <typename T>
void dialog(T*& d) noexcept {
    if (!d) {
        return;
    }

    stl::ObjPool* pool = d->pool;

    d = nullptr;
    stl::RefCountOps<stl::ObjPool>::unref(pool);
}

template <typename T, typename F>
void dialog(bool toggle, T*& d, F&& draw) {
    if (toggle) {
        if (d) {
            dialog(d);
        } else {
            auto pool = stl::ObjPool::fromMemory();
            stl::ObjPool* p = pool.mutPtr();

            d = p->make<T>(p);
            p->ref();
        }
    }

    if (!d) {
        return;
    }

    bool open = true;

    draw(*d, open);

    if (!open) {
        dialog(d);
    }
}
