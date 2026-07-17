#pragma once

#include "pooled.h"

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
void dialog(stl::ObjPool& owner, bool toggle, void** state, F&& draw) {
    T*& d = *(T**)state;

    if (toggle) {
        if (d) {
            dialog(d);
        } else {
            auto pool = stl::ObjPool::fromMemory();
            stl::ObjPool* p = pool.mutPtr();

            d = p->make<T>(p);
            p->ref();
            pooledGuard(owner, [state] {
                T*& d = *(T**)state;

                dialog(d);
            });
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
