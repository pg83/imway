#include "main_composer.h"
#include "main_screenshot.h"
#include "main_supervisor.h"

#include <std/str/view.h>

using namespace stl;

int main(int argc, char** argv) {
    // multi-call: `imway screenshot <path>` is the crop tool, not the
    // compositor
    if (argc >= 3 && StringView(argv[1]) == StringView("screenshot")) {
        return mainScreenshot(StringView(argv[2]));
    }

    if (argc >= 2 && StringView(argv[1]) == StringView("composer")) {
        return mainComposer(argc - 1, argv + 1);
    }

    return mainSupervisor(argc, argv);
}
