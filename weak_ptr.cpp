#include "weak_ptr.h"

#include <std/dbg/verify.h>
#include <std/lib/list.h>

// The ring is a circular IntrusiveNode list. stl gives us everything: unlink()
// is idempotent (no-op when already singular, and leaves the node singular),
// and insertAfter() unlinks its node before splicing it in, so bind() need not
// detach by hand.

WeakRefBase::WeakRefBase(void* p) noexcept
    : ptr(p) {
}

WeakRefBase::WeakRefBase(WeakRefBase& o) noexcept
    : ptr(o.ptr) {
    stl::IntrusiveList::insertAfter(&o, this);
}

WeakRefBase::~WeakRefBase() noexcept {
    unlink();
}

void WeakRefBase::bind(WeakRefBase& o) noexcept {
    ptr = o.ptr;
    stl::IntrusiveList::insertAfter(&o, this);
}

void WeakRefBase::reset() noexcept {
    unlink();
    ptr = nullptr;
}

void WeakRefBase::invalidate() noexcept {
    for (stl::IntrusiveNode* n = next; n != this; n = n->next) {
        ((WeakRefBase*)n)->ptr = nullptr;
    }

    ptr = nullptr;
    unlink();
}

void* WeakRefBase::getNoNull() const {
    STD_VERIFY(ptr);

    return ptr;
}
