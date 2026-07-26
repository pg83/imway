#include "settings.h"

#include "composer.h"
#include "keyboard.h"
#include "listener.h"
#include "intr_list.h"

#include <std/dbg/assert.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>

using namespace stl;

#include "settings.impl.gen.inc"

void applySettingsEnvironment(Settings& settings) {
    if (const char* terminal = getenv("IMWAY_TERMINAL"); terminal && *terminal) {
        settings.setTerminal(StringView(terminal));
    } else if (const char* terminal = getenv("TERMINAL"); terminal && *terminal) {
        settings.setTerminal(StringView(terminal));
    }
}
