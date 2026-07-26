#include "settings.h"

#include <stdlib.h>

using namespace stl;

void applySettingsEnvironment(Settings& settings) {
    if (const char* terminal = getenv("IMWAY_TERMINAL"); terminal && *terminal) {
        settings.setTerminal(StringView(terminal));
    } else if (const char* terminal = getenv("TERMINAL"); terminal && *terminal) {
        settings.setTerminal(StringView(terminal));
    }
}
