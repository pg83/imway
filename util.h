#pragma once

#include <string.h>

#include <std/dbg/verify.h>
#include <std/ios/out_zc.h>
#include <std/lib/vector.h>
#include <std/str/view.h>

inline stl::StringView operator""_sv(const char* s, size_t len) {
    return {(const u8*)s, len};
}

// fixed-capacity stream producing a NUL-terminated string for C APIs;
// string writes are truncated to fit, number writes must fit whole:
// libstd requests 24 spare bytes per integer (128 per float) up front,
// so size N with that headroom, not just for the expected characters
template <size_t N>
class CStr: public stl::ZeroCopyOutput {
    char buf[N];
    size_t used = 0;

    size_t writeImpl(const void* data, size_t len) override {
        size_t avail = N - 1 - used;
        size_t n = len < avail ? len : avail;

        memcpy(buf + used, data, n);
        used += n;

        return len;
    }

    void* imbueImpl(size_t* len) override {
        STD_VERIFY(used + *len < N);

        return buf + used;
    }

    void commitImpl(size_t len) override {
        used += len;
    }

public:
    const char* cStr() noexcept {
        buf[used] = 0;

        return buf;
    }

    stl::StringView view() const noexcept {
        return {(const u8*)buf, used};
    }
};

double parseFloat(stl::StringView s);

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
