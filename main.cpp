#include "coverage.h"
#include "main_composer.h"
#include "main_screenshot.h"

#include <std/str/view.h>

#ifdef IMWAY_FOR_TESTS
#include <stdlib.h>
#include <pthread.h>
#endif

using namespace stl;

#ifdef IMWAY_FOR_TESTS
extern "C" void __llvm_profile_set_filename(const char* pattern) __attribute__((weak));
#endif

namespace {
#ifdef IMWAY_FOR_TESTS
    // The runtime expands %p in the profile file name once, at start: a
    // fork() child (libseat's embedded seatd, a spawned client before its
    // exec) inherits the parent's pid in it and writes its profile over the
    // parent's, and when both exit together neither loads. Setting the
    // pattern again in the child names the file after the child. The
    // runtime skips a pattern equal to the one it holds, so another one goes
    // first to make it parse the real one anew.
    void profileForkChild() {
        const char* pattern = getenv("LLVM_PROFILE_FILE");

        if (pattern && __llvm_profile_set_filename) {
            __llvm_profile_set_filename("imway-fork-child.profraw");
            __llvm_profile_set_filename(pattern);
        }
    }
#endif

    int withProfileFlush(int rc) {
        flushCoverage();

        return rc;
    }
}

int main(int argc, char** argv) {
#ifdef IMWAY_FOR_TESTS
    pthread_atfork(nullptr, nullptr, profileForkChild);
#endif

    // multi-call: `imway screenshot <path>` is the crop tool, not the
    // compositor
    if (argc >= 3 && StringView(argv[1]) == StringView("screenshot")) {
        return withProfileFlush(mainScreenshot(StringView(argv[2])));
    }

    return withProfileFlush(mainComposer(argc, argv));
}
