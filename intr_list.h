#pragma once

#include <std/lib/list.h>

// typed ranged-for over stl::IntrusiveList, visitor.h-style adapter: the
// element type T inherits (a tag-base deriving from) stl::IntrusiveNode, N
// names that base when T is a member of several lists at once. Iteration is
// not removal-safe — unlink the current node only after stepping past it.
template <typename T, typename N>
struct IntrListEach {
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
IntrListEach<T, N> each(stl::IntrusiveList& l) {
    return {l};
}

// the same, back to front (z-order picks walk topmost first)
template <typename T, typename N>
struct IntrListEachRev {
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
IntrListEachRev<T, N> eachRev(stl::IntrusiveList& l) {
    return {l};
}

template <typename T, typename N = T, typename F>
void forEach(stl::IntrusiveList& l, F f) {
    // step first so the callback may unlink the current element
    for (stl::IntrusiveNode* n = l.mutFront(); n != l.mutEnd();) {
        stl::IntrusiveNode* cur = n;

        n = n->next;
        f(*(T*)(N*)cur);
    }
}

template <typename T, typename N = T, typename F>
void forEachRev(stl::IntrusiveList& l, F f) {
    // same guarantee in reverse
    for (stl::IntrusiveNode* n = l.mutBack(); n != l.mutEnd();) {
        stl::IntrusiveNode* cur = n;

        n = n->prev;
        f(*(T*)(N*)cur);
    }
}

// membership by pointer compare — the candidate is never dereferenced, so a
// dangling pointer is a legal input (the classic "is it still alive" probe)
template <typename T, typename N = T>
bool intrListContains(const stl::IntrusiveList& l, const T* p) {
    for (const stl::IntrusiveNode* n = l.front(); n != l.end(); n = n->next) {
        if ((const T*)(const N*)n == p) {
            return true;
        }
    }

    return false;
}
