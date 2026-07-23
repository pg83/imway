#include "crash_trace.h"

#ifdef IMWAY_FILL_GARBAGE

    #include <csignal>
    #include <cstdint>
    #include <cstdio>
    #include <cstring>

    #include <unistd.h>

// A frame-pointer stack walk for the poisoned test build (imway_test is built
// with -fno-omit-frame-pointer and is not stripped). It prints raw pc plus the
// file-relative offset of each return address, so an external
//   addr2line -e .build/imway_test -f -C <off>
// resolves it. cpptrace's _Unwind backend yields nothing under static musl;
// walking rbp does not depend on the C++ unwinder at all.

namespace {
    uintptr_t exeBase() {
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

        if (n <= 0) {
            return 0;
        }

        exe[n] = 0;

        FILE* f = fopen("/proc/self/maps", "r");

        if (!f) {
            return 0;
        }

        char line[4096];
        uintptr_t base = 0;

        while (fgets(line, sizeof(line), f)) {
            uintptr_t start = 0, end = 0;
            char perms[8] = "";
            char path[4096] = "";

            if (sscanf(line, "%lx-%lx %7s %*x %*x:%*x %*u %4095[^\n]", &start, &end, perms, path) >= 3) {
                const char* p = path;

                while (*p == ' ') {
                    p++;
                }

                if (strcmp(p, exe) == 0) {
                    base = start;
                    break;
                }
            }
        }

        fclose(f);

        return base;
    }

    void crashHandler(int sig) {
        uintptr_t base = exeBase();

        std::fprintf(
            stderr,
            "\n=== imway crash: signal %d  exe_base=0x%lx"
            "  (addr2line -e .build/imway_test -f -C <off>) ===\n",
            sig,
            (unsigned long)base
        );

        void** fp = (void**)__builtin_frame_address(0);

        for (int i = 0; i < 64 && fp; i++) {
            void* ret = fp[1];

            if (!ret) {
                break;
            }

            uintptr_t a = (uintptr_t)ret;

            std::fprintf(stderr, "  #%02d  pc=0x%lx  off=0x%lx\n", i, (unsigned long)a, (unsigned long)(base ? a - base : a));

            void** next = (void**)fp[0];

            if (next <= fp) {
                break;
            }

            fp = next;
        }

        std::fflush(stderr);
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }
}

void installCrashTracer() {
    static const int sigs[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE};

    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        std::signal(sigs[i], crashHandler);
    }
}

#else

void installCrashTracer() {
}

#endif
