#pragma once

#include <std/alg/destruct.h>
#include <std/mem/embed.h>
#include <std/mem/new.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

// objects above this size do not belong here: they get their own lifetime
// story (a pool, a FrameResource), not a recycled small-object slot
inline constexpr size_t kSmallObjMaxSize = 2048;

// the composer's one small-object allocator: every dynamic-lifetime
// protocol/desktop object allocates here. Exponential size classes
// (16 bytes .. kSmallObjMaxSize), each a lazily created free list in the
// composer pool; released memory returns to its class for reuse, the
// classes themselves die with the pool
struct SmallObjAllocator {
    virtual void* allocate(size_t len) = 0;
    virtual void deallocate(void* ptr, size_t len) = 0;

    template <typename T, typename... A>
    T* make(A&&... a) {
        struct TT: public stl::Embed<T>, public stl::Newable {
            using stl::Embed<T>::Embed;
        };

        static_assert(sizeof(TT) == sizeof(T));
        static_assert(sizeof(T) <= kSmallObjMaxSize, "too big for the small-object allocator: own a pool instead");
        static_assert(alignof(T) <= alignof(max_align_t));

        return &(new (allocate(sizeof(T))) TT(stl::forward<A>(a)...))->t;
    }

    template <typename T>
    void release(T* t) {
        deallocate(stl::destruct(t), sizeof(T));
    }

    static SmallObjAllocator* create(stl::ObjPool* pool);
};
