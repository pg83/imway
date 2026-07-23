// The log ring against a reference model: random writes in random chunkings
// must yield exactly the lines a naive implementation keeps. Covers the
// wrap, the unfinished-line relocation, entry invalidation as the ring
// overwrites, the line-index cap and the oversized-line split.

#include "log.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/rng/split_mix_64.h>
#include <std/str/builder.h>
#include <std/str/view.h>

using namespace stl;

namespace {
    // mirror of the implementation constants (log.cpp asserts them)
    constexpr size_t kBuf = size_t(1) << 17;
    constexpr size_t kLines = size_t(1) << 13;

    struct Model {
        // a flat copy of every byte ever written; line starts and lengths
        // follow the same split rule as the implementation
        StringBuilder bytes;

        // parallel arrays: start offset into `bytes` and length
        struct Line {
            size_t start = 0;
            size_t len = 0;
        };

        Vector<Line> lines;
        size_t lineStart = 0;

        void write(StringView chunk) {
            for (u8 b : chunk) {
                if (b == '\n') {
                    lines.pushBack({lineStart, bytes.used() - lineStart});
                    bytes << StringView(&b, 1);
                    lineStart = bytes.used();

                    continue;
                }

                bytes << StringView(&b, 1);

                if (bytes.used() - lineStart == kLogMaxLine) {
                    // oversized line: split, the continuation starts here
                    lines.pushBack({lineStart, kLogMaxLine});
                    lineStart = bytes.used();
                }
            }
        }
    };

    u64 rng = 0x1badf00d;

    u64 next() {
        return splitMix64(++rng);
    }

    bool check(Log& log, const Model& model) {
        size_t kept = log.histLen();

        if (kept > model.lines.length()) {
            fprintf(stderr, "kept %zu lines, model has only %zu\n", kept, model.lines.length());

            return false;
        }

        // the implementation's history must be exactly the newest `kept`
        // model lines, byte for byte
        for (size_t i = 0; i < kept; i++) {
            const Model::Line& want = model.lines[model.lines.length() - kept + i];
            StringView got = log.histElem(i);
            StringView expect((const u8*)model.bytes.data() + want.start, want.len);

            if (got != expect) {
                fprintf(stderr, "line %zu of %zu mismatch: got %zu bytes, want %zu bytes\n", i, kept, got.length(), expect.length());

                return false;
            }
        }

        return true;
    }

    // how many of the newest model lines the implementation MUST still hold:
    // every line whose whole span lies within the last kBuf/2 bytes of the
    // stream and within the last kLines/2 entries is unconditionally alive
    // (padding at wraps can consume at most the tail skipped per wrap, and
    // eviction only trims the cap — half margins are safely conservative)
    size_t mustKeep(const Model& model) {
        size_t total = model.bytes.used();
        size_t n = 0;

        while (n < model.lines.length() && n < kLines / 2) {
            const Model::Line& line = model.lines[model.lines.length() - 1 - n];

            if (total - line.start > kBuf / 2) {
                break;
            }

            n++;
        }

        return n;
    }
}

int main() {
    ObjPool::Ref pool = ObjPool::fromMemory();

    for (int round = 0; round < 8; round++) {
        Log* log = Log::create(pool.mutPtr(), nullptr);
        Model model;
        StringBuilder chunk;

        // newline density decides which eviction dominates: dense newlines
        // hit the kLines cap, sparse ones age lines out of the byte window
        u64 nl = round % 3 == 0 ? 10 : round % 3 == 1 ? 60 : 400;

        for (int step = 0; step < 20000; step++) {
            chunk.reset();

            // a write: random length, random newline density, sometimes
            // pathological shapes
            u64 shape = next() % 100;
            size_t len;

            if (shape < 5) {
                len = 0; // empty write
            } else if (shape < 10) {
                len = 1; // single byte, often a lone newline
            } else if (shape < 85) {
                len = 1 + next() % 120; // ordinary log line
            } else if (shape < 97) {
                len = 200 + next() % 4000; // long line
            } else {
                len = kLogMaxLine + next() % kLogMaxLine; // oversized
            }

            for (size_t i = 0; i < len; i++) {
                u64 r = next();
                char b = r % nl == 0 ? '\n' : (char)('a' + r % 26);

                chunk << StringView((const u8*)&b, 1);
            }

            if (shape >= 10 && len && next() % 2) {
                char nl = '\n';

                chunk << StringView((const u8*)&nl, 1);
            }

            // feed it through the stream operator in random sub-chunks,
            // exercising both the direct-imbue and the writeImpl paths
            StringView rest = sv(chunk);

            while (!rest.empty()) {
                size_t piece = 1 + next() % (next() % 8 == 0 ? 1 : rest.length());

                if (piece > rest.length()) {
                    piece = rest.length();
                }

                *log << rest.prefix(piece);
                rest = rest.suffix(rest.length() - piece);
            }

            model.write(sv(chunk));

            // numbers go through the direct-imbue path (24/128 bytes), the
            // only one that triggers the wrap relocation from imbue
            if (next() % 4 == 0) {
                u64 v = next();
                StringBuilder rendered;

                *log << v << "\n"_sv;
                rendered << v << "\n"_sv;
                model.write(sv(rendered));
            }

            if (step % 8 == 0 || step > 19900) {
                if (!check(*log, model)) {
                    fprintf(stderr, "round %d step %d\n", round, step);

                    return 1;
                }

                if (log->histLen() < mustKeep(model)) {
                    fprintf(stderr, "round %d step %d: kept %zu, must keep %zu\n", round, step, log->histLen(), mustKeep(model));

                    return 1;
                }
            }
        }

        if (!check(*log, model)) {
            return 1;
        }
    }

    // directed edges: exact-boundary writes
    {
        Log* log = Log::create(pool.mutPtr(), nullptr);
        Model model;

        // long lines interleaved with short ones drive many wraps and
        // relocations right at the physical boundary
        for (int i = 0; i < 40; i++) {
            StringBuilder line;
            size_t len = i % 2 ? kBuf / 3 : 17;

            for (size_t j = 0; j < len; j++) {
                char b = (char)('A' + (i + j) % 26);

                line << StringView((const u8*)&b, 1);
            }

            char nl = '\n';

            line << StringView((const u8*)&nl, 1);
            *log << sv(line);
            model.write(sv(line));

            if (!check(*log, model)) {
                fprintf(stderr, "directed step %d\n", i);

                return 1;
            }
        }
    }

    printf("log ring: ok\n");

    return 0;
}
