#include "main_composer.h"
#include "main_screenshot.h"
#include "main_supervisor.h"

#include <std/str/view.h>

using namespace stl;

// present only in coverage-instrumented builds; the runtime's own at-exit
// hook is not reliable under every harness, so flush at the natural end
extern "C" int __llvm_profile_write_file(void) __attribute__((weak));

namespace {
    int withProfileFlush(int rc) {
        if (&__llvm_profile_write_file) {
            __llvm_profile_write_file();
        }

        return rc;
    }
}

int main(int argc, char** argv) {
    // multi-call: `imway screenshot <path>` is the crop tool, not the
    // compositor
    if (argc >= 3 && StringView(argv[1]) == StringView("screenshot")) {
        return withProfileFlush(mainScreenshot(StringView(argv[2])));
    }

    if (argc >= 2 && StringView(argv[1]) == StringView("composer")) {
        return withProfileFlush(mainComposer(argc - 1, argv + 1));
    }

    return withProfileFlush(mainSupervisor(argc, argv));
}
