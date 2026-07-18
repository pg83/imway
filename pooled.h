#pragma once

#include <std/mem/obj_pool.h>

// Pool-owned resources: registered into an entity's ObjPool they release
// when the pool dies — LIFO with their siblings, so register in acquisition
// order and dependents die first. An impl is submitted AFTER its constructor
// returns, so cleanups registered inside the constructor run after the
// impl's own destructor: they must be self-contained (captured values, own
// boxes) and must never call back into the impl. One-shot resources go through plain
// registration functions. Dedicated fd and event-loop wrappers live in
// pooled_fd.h and pooled_ev.h.

// a one-off cleanup adopted by the pool: the escape hatch for foreign
// resources without a dedicated registration. Capture by value — by
// pool-death time the registering scope is long gone.
template <typename F>
void pooledGuard(stl::ObjPool& p, F f) {
    struct Guard {
        F f;

        Guard(F g)
            : f(g)
        {
        }

        ~Guard() noexcept {
            f();
        }
    };

    p.make<Guard>(f);
}
