#include "small_obj_allocator.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/free_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr size_t kMinSize = 16;
    constexpr size_t kClasses = 8; // 16 << 7 == kSmallObjMaxSize

    static_assert(kMinSize << (kClasses - 1) == kSmallObjMaxSize);

    // ceil-log2 of len clamped up to kMinSize, rebased so 16 -> 0
    size_t classFor(size_t len) {
        return 64 - (size_t)__builtin_clzll((len - 1) | (kMinSize - 1)) - 4;
    }

    struct SmallObjAllocatorImpl: public SmallObjAllocator {
        ObjPool* pool = nullptr;
        FreeList* classes[kClasses] = {};
        i64 live = 0;

        SmallObjAllocatorImpl(ObjPool* p);
        // the allocator is the composer's first component, so this runs
        // after every subsystem destructor: a nonzero balance is an object
        // someone forgot to release, and that is a bug in any build
        ~SmallObjAllocatorImpl() noexcept;

        void* allocate(size_t len) override;
        void deallocate(void* ptr, size_t len) override;
    };
}

SmallObjAllocatorImpl::~SmallObjAllocatorImpl() noexcept {
    if (live != 0) {
        sysE << "imway: small-object allocator torn down with "_sv << live << " live objects"_sv << endL;
        abort();
    }
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

    live++;

    void* ptr = fl->allocate();

#ifdef IMWAY_FOR_TESTS
    // hand out poison so a field the constructor forgets to set reads as an
    // obvious sentinel (0xAB...), not a plausible zero
    memset(ptr, 0xAB, kMinSize << classFor(len));
#endif

    return ptr;
}

void SmallObjAllocatorImpl::deallocate(void* ptr, size_t len) {
    live--;

#ifdef IMWAY_FOR_TESTS
    // poison the freed slot before it re-enters the free list, so a
    // use-after-free read returns 0xDE... instead of stale-but-valid data.
    // FreeList::release rewrites the first bytes with its next pointer; the
    // rest of the object stays poisoned
    memset(ptr, 0xDE, kMinSize << classFor(len));
#endif

    classes[classFor(len)]->release(ptr);
}

SmallObjAllocator* SmallObjAllocator::create(ObjPool* pool) {
    return pool->make<SmallObjAllocatorImpl>(pool);
}
