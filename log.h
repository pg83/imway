#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>
#include <std/ios/out_zc.h>

namespace stl {
    class ObjPool;
}

// The in-memory log every subsystem writes to through Composer::log. Bytes
// tee through to the given output (stderr: the IX log and the test harness
// greps) and land in a ring that keeps the recent history as whole lines
// for the log widget. histElem(0) is the oldest kept line, without its
// newline; the view is valid until the next write. A line longer than
// kLogMaxLine is split into consecutive entries; an unfinished line is not
// in the history until its newline arrives.
inline constexpr size_t kLogMaxLine = size_t(1) << 14;

struct Log: stl::ZeroCopyOutput {
    virtual size_t histLen() = 0;
    virtual stl::StringView histElem(size_t n) = 0;

    // tee may be null (unit tests): the ring is then the only destination
    static Log* create(stl::ObjPool* pool, stl::Output* tee);
};

// stderr through its own non-blocking descriptor: a stalled reader on the
// other end of the pipe must not stall the compositor. Whatever does not
// fit is dropped — the ring above keeps the full history.
stl::Output* nonblockStderr(stl::ObjPool* pool);
