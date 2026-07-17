#include "main_composer.h"
#include "main_screenshot.h"

#include <std/str/view.h>

using namespace stl;

int main(int argc, char** argv) {
    // multi-call: `imway screenshot <path>` is the crop tool, not the
    // compositor
    if (argc >= 3 && StringView(argv[1]) == StringView("screenshot")) {
        return mainScreenshot(StringView(argv[2]));
    }

    return mainComposer(argc, argv);
}
