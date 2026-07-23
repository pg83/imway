#include "log.h"

#include <string.h>

#include <std/dbg/verify.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr u32 kBuf = u32(1) << 17;
    constexpr u32 kLines = u32(1) << 13;

    // powers of two: u32 truncation is compatible with & (k - 1), so the
    // monotonic logical cursors live in plain u32 modular arithmetic
    static_assert((kBuf & (kBuf - 1)) == 0);
    static_assert((kLines & (kLines - 1)) == 0);
    // an unfinished line relocates to the buffer start at a wrap: it must be
    // small against the buffer so the copy is cheap and can never overlap
    static_assert(kLogMaxLine <= kBuf / 8);

    struct LineRef {
        u32 start = 0; // logical offset of the line's first byte
        u32 len = 0;   // bytes, the newline excluded
    };

    struct LogImpl: public Log {
        Output* tee = nullptr;

        // logical write cursor. It advances over written bytes AND over the
        // tail padding skipped at a wrap, so physical == logical & (kBuf - 1)
        // holds always and a byte is alive iff wpos - offset <= kBuf.
        u32 wpos = 0;
        // logical start of the unfinished line, physically contiguous by
        // construction; not in the history until its newline arrives
        u32 lineStart = 0;

        // the ring of finalized lines: monotonically numbered, slot n & mask
        LineRef lines[kLines];
        u32 lineHead = 0;
        u32 lineTail = 0;

        char buf[kBuf];

        LogImpl(Output* t);

        void* imbueImpl(size_t* avail) override;
        void commitImpl(size_t len) override;

        size_t histLen() override;
        StringView histElem(size_t n) override;

        void relocateUnfinished();
        void push(u32 start, u32 len);
        void dropDead();
    };
}

LogImpl::LogImpl(Output* t)
    : tee(t)
{
}

// pad the skipped tail as if written (physical == logical & mask survives)
// and move the unfinished line's bytes to the buffer start, so a line never
// splits across the wrap and histElem stays a single view
void LogImpl::relocateUnfinished() {
    u32 unf = wpos - lineStart;
    u32 physStart = lineStart & (kBuf - 1);

    wpos += (kBuf - (wpos & (kBuf - 1))) & (kBuf - 1);
    // no overlap: unf <= kBuf/8 while the source sits in the last quarter
    memcpy(buf, buf + physStart, unf);
    lineStart = wpos;
    wpos += unf;
    dropDead();
}

void* LogImpl::imbueImpl(size_t* avail) {
    size_t need = *avail;

    // direct formatter imbues are tiny (numbers: 24, floats: 128); bulk
    // strings arrive through writeImpl, which asks for 1 byte and adapts to
    // whatever tail we expose
    STD_VERIFY(need <= kBuf / 4);

    u32 tail = kBuf - (wpos & (kBuf - 1));

    if (tail < need) {
        relocateUnfinished();
        tail = kBuf - (wpos & (kBuf - 1));
    }

    *avail = tail;

    return buf + (wpos & (kBuf - 1));
}

void LogImpl::commitImpl(size_t len) {
    if (!len) {
        return;
    }

    const char* chunk = buf + (wpos & (kBuf - 1));

    if (tee) {
        tee->write(chunk, len);
    }

    for (size_t i = 0; i < len; i++) {
        wpos++;

        if (chunk[i] == '\n') {
            push(lineStart, wpos - 1 - lineStart);
            lineStart = wpos;
        } else if (wpos - lineStart == kLogMaxLine) {
            // oversized line: split, the continuation starts right here
            push(lineStart, kLogMaxLine);
            lineStart = wpos;
        }
    }

    dropDead();

    // a bulk write can land exactly on the physical end mid-line: relocate
    // now, before the next chunk would continue the line at the start
    if (wpos != lineStart && (wpos & (kBuf - 1)) == 0) {
        relocateUnfinished();
    }
}

void LogImpl::push(u32 start, u32 len) {
    if (lineTail - lineHead == kLines) {
        lineHead++; // the cap: the oldest line falls off
    }

    lines[lineTail & (kLines - 1)] = {start, len};
    lineTail++;
}

void LogImpl::dropDead() {
    while (lineTail != lineHead) {
        const LineRef& ref = lines[lineHead & (kLines - 1)];

        if (wpos - ref.start <= kBuf) {
            break;
        }

        lineHead++;
    }
}

size_t LogImpl::histLen() {
    return lineTail - lineHead;
}

StringView LogImpl::histElem(size_t n) {
    STD_VERIFY(n < histLen());

    const LineRef& ref = lines[(lineHead + (u32)n) & (kLines - 1)];

    return StringView((const u8*)buf + (ref.start & (kBuf - 1)), ref.len);
}

Log* Log::create(ObjPool* pool, Output* tee) {
    return pool->make<LogImpl>(tee);
}
