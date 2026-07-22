#pragma once

#include <std/lib/node.h>

// A back-pointer that unlinks itself. Every reference aimed at one target is
// threaded into a single ring (each is an IntrusiveNode); when the target dies
// it calls invalidate() on its anchor, the ring is walked, and every pointer
// in it is nulled. A lone reference dying just leaves the ring. There is no
// copy constructor: a new reference is spun off an existing node, taking that
// node's pointer and linking into its ring.
//
// Usage: the target holds an anchor reference pointing at itself (seat it with
// anchor(self) right after allocation) and calls invalidate() from its destroy
// handler. Observers hold their own reference and bind() it to the anchor; they
// read through get(), which returns null once the target is gone. This turns
// the whole class of manual "sever every back-pointer in the destroy handler"
// bookkeeping into something the ring does on its own.
//
// The ring logic is type-erased in WeakRefBase (implemented in weak_ptr.cpp);
// the Weak<T> template only adds typed casts over it.
struct WeakRefBase: stl::IntrusiveNode {
    void* ptr = nullptr;

    WeakRefBase() noexcept = default;
    explicit WeakRefBase(void* p) noexcept;
    WeakRefBase(WeakRefBase& o) noexcept;

    WeakRefBase(const WeakRefBase&) = delete;
    WeakRefBase& operator=(const WeakRefBase&) = delete;
    WeakRefBase& operator=(WeakRefBase&&) = delete;

    ~WeakRefBase() noexcept;

    // repoint at whatever o points at, joining o's ring
    void bind(WeakRefBase& o) noexcept;

    // leave the ring and forget the target
    void reset() noexcept;

    // the target is dying: null every pointer in the ring, then leave it
    void invalidate() noexcept;

    // the pointer, but abort if it is dead: for operator->/operator* where a
    // live target is a precondition. Out of line so the check is not inlined
    // into every Weak<T> instantiation.
    void* getNoNull() const;
};

template <typename T>
struct Weak: WeakRefBase {
    Weak() noexcept = default;

    // seat an anchor on its target (or make an observer aimed straight at t)
    explicit Weak(T* t) noexcept
        : WeakRefBase((void*)t)
    {
    }

    // spin a new reference off an existing node
    Weak(Weak& o) noexcept
        : WeakRefBase(o)
    {
    }

    void bind(Weak& o) noexcept {
        WeakRefBase::bind(o);
    }

    // (re)seat an anchor at its target without touching the ring
    void anchor(T* t) noexcept {
        ptr = (void*)t;
    }

    // nullable read: use in guards and comparisons
    T* get() const noexcept {
        return (T*)ptr;
    }

    // dereference: aborts if the target is dead (a live one is a precondition)
    T* operator->() const {
        return (T*)getNoNull();
    }

    T& operator*() const {
        return *(T*)getNoNull();
    }

    explicit operator bool() const noexcept {
        return ptr != nullptr;
    }
};
