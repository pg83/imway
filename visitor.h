#pragma once

#include <std/lib/visitor.h>

// typed adapter over libstd's void* VisitorFace (std/lib/visitor.h). the
// enumerate scheme: an interface exposes a template forEach-style method
// that funnels into a virtual taking stl::VisitorFace&&, the impl calls
// vis.visit(&item) once per element, and this casts the void* back to the
// concrete type — so call sites pass a typed lambda straight in, the way
// listDir and DirVisitor do. the visited element arrives as a T&
template <typename T, typename F>
auto visitEach(F f) {
    // non-const T&, matching listDir: the element is the collection's own,
    // handed out for reading (StringBuilder::cStr appends a NUL, not const)
    return stl::makeVisitor([f](void* p) {
        f(*(T*)p);
    });
}
