#pragma once

#include <std/mem/obj_pool.h>

// The public handle owns one self-contained arena.  `opaque` is the widget's
// private state; users may destroy the entire dialog at any point without
// knowing its concrete type by calling dialog(state).
struct DialogState {
    stl::ObjPool* pool = nullptr;
    void* opaque = nullptr;
};

inline void dialog(DialogState*& state) noexcept {
    if (!state) {
        return;
    }

    stl::ObjPool* pool = state->pool;

    state = nullptr;
    delete pool;
}

template <typename T, typename F>
void dialog(bool toggle, DialogState** slot, F&& draw) {
    DialogState*& state = *slot;

    if (toggle) {
        if (state) {
            dialog(state);
        } else {
            stl::ObjPool* pool = stl::ObjPool::fromMemoryRaw();
            DialogState* created = pool->make<DialogState>();

            created->pool = pool;
            created->opaque = pool->make<T>();
            state = created;
        }
    }

    if (!state) {
        return;
    }

    bool open = true;

    draw(*(T*)state->opaque, open);

    if (!open) {
        dialog(state);
    }
}
