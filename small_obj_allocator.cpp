#include "small_obj_allocator.h"

#include <std/dbg/verify.h>
#include <std/mem/free_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr size_t kMinSize = 16;
    constexpr size_t kClasses = 7; // 16 << 6 == kSmallObjMaxSize

    static_assert(kMinSize << (kClasses - 1) == kSmallObjMaxSize);

    // ceil-log2 of len clamped up to kMinSize, rebased so 16 -> 0
    size_t classFor(size_t len) {
        return 64 - (size_t)__builtin_clzll((len - 1) | (kMinSize - 1)) - 4;
    }

    struct SmallObjAllocatorImpl: public SmallObjAllocator {
        ObjPool* pool = nullptr;
        FreeList* classes[kClasses] = {};

        SmallObjAllocatorImpl(ObjPool* p);

        void* allocate(size_t len) override;
        void deallocate(void* ptr, size_t len) override;
    };
}

SmallObjAllocatorImpl::SmallObjAllocatorImpl(ObjPool* p)
    : pool(p)
{
}

void* SmallObjAllocatorImpl::allocate(size_t len) {
    FreeList*& fl = classes[classFor(len)];

    if (!fl) {
        fl = FreeList::create(pool, kMinSize << classFor(len));
    }

    return fl->allocate();
}

void SmallObjAllocatorImpl::deallocate(void* ptr, size_t len) {
    classes[classFor(len)]->release(ptr);
}

SmallObjAllocator* SmallObjAllocator::create(ObjPool* pool) {
    return pool->make<SmallObjAllocatorImpl>(pool);
}
