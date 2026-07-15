#include "icon.h"
#include "icon_pool.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct IconPoolImpl: public IconPool {
        ObjPool* pool = nullptr;
        Vector<Icon*> freeList;
        u64 gen = 0;

        IconPoolImpl(ObjPool* p)
            : pool(p)
        {
        }

        Icon* acquire() override {
            Icon* ic = freeList.empty() ? pool->make<Icon>() : freeList.popBack();

            ic->gen = ++gen;
            ic->width = 0;
            ic->height = 0;
            ic->argb.clear();

            return ic;
        }

        void release(Icon* icon) override {
            if (icon) {
                freeList.pushBack(icon);
            }
        }
    };
}

IconPool* IconPool::create(ObjPool* pool) {
    return pool->make<IconPoolImpl>(pool);
}
