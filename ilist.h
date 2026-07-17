#pragma once

#include <std/lib/list.h>

// typed ranged-for over stl::IntrusiveList, visitor.h-style adapter: the
// element type T inherits (a tag-base deriving from) stl::IntrusiveNode, N
// names that base when T is a member of several lists at once. Iteration is
// not removal-safe — unlink the current node only after stepping past it.
template <typename T, typename N>
struct IListEach {
    stl::IntrusiveList& list;

    struct It {
        stl::IntrusiveNode* n;

        bool operator!=(const It& r) const {
            return n != r.n;
        }

        void operator++() {
            n = n->next;
        }

        T* operator*() const {
            return (T*)(N*)n;
        }
    };

    It begin() const {
        return {list.mutFront()};
    }

    It end() const {
        return {list.mutEnd()};
    }
};

template <typename T, typename N = T>
IListEach<T, N> each(stl::IntrusiveList& l) {
    return {l};
}

// the same, back to front (z-order picks walk topmost first)
template <typename T, typename N>
struct IListEachRev {
    stl::IntrusiveList& list;

    struct It {
        stl::IntrusiveNode* n;

        bool operator!=(const It& r) const {
            return n != r.n;
        }

        void operator++() {
            n = n->prev;
        }

        T* operator*() const {
            return (T*)(N*)n;
        }
    };

    It begin() const {
        return {list.mutBack()};
    }

    It end() const {
        return {list.mutEnd()};
    }
};

template <typename T, typename N = T>
IListEachRev<T, N> eachRev(stl::IntrusiveList& l) {
    return {l};
}
