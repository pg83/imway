#pragma once

#include <string.h>

#include <std/lib/vector.h>
#include <std/str/view.h>

inline stl::StringView operator""_sv(const char* s, size_t len) {
    return {(const u8*)s, len};
}

u32 nowMsec();

template <typename T>
bool removeOne(stl::Vector<T>& v, const T& t) {
    for (size_t i = 0; i < v.length(); i++) {
        if (v[i] == t) {
            memmove(v.mutData() + i, v.data() + i + 1, (v.length() - i - 1) * sizeof(T));
            v.popBack();

            return true;
        }
    }

    return false;
}

template <typename T>
void insertAt(stl::Vector<T>& v, size_t idx, const T& t) {
    v.pushBack(t);
    memmove(v.mutData() + idx + 1, v.data() + idx, (v.length() - 1 - idx) * sizeof(T));
    v.mut(idx) = t;
}

template <typename T>
bool contains(const stl::Vector<T>& v, const T& t) {
    for (size_t i = 0; i < v.length(); i++) {
        if (v[i] == t) {
            return true;
        }
    }

    return false;
}

template <typename T>
long indexOf(const stl::Vector<T>& v, const T& t) {
    for (size_t i = 0; i < v.length(); i++) {
        if (v[i] == t) {
            return (long)i;
        }
    }

    return -1;
}
