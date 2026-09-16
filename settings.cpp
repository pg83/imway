#include "settings.h"

#include "util.h"
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

#ifdef IMWAY_FOR_TESTS
#include "settings.control.gen.inc"

namespace {
    // IMWAY_SETTINGS=key=value;key=value — startup-time settings for
    // scenarios, applied before any subsystem reads them
    void applySettingsFromEnvironment(Settings& settings) {
        const char* spec = getenv("IMWAY_SETTINGS");

        if (!spec || !*spec) {
            return;
        }

        StringView rest(spec);

        while (!rest.empty()) {
            StringView entry, tail;

            if (rest.split(';', entry, tail)) {
                rest = tail;
            } else {
                entry = rest;
                rest = {};
            }

            StringView key, value;

            if (entry.empty() || !entry.split('=', key, value)) {
                continue;
            }

            applySettingText(settings, key, value);
        }
    }
}
#endif

void applySettingsEnvironment(Settings& settings) {
    if (const char* terminal = getenv("IMWAY_TERMINAL"); terminal && *terminal) {
        settings.setTerminal(StringView(terminal));
    } else if (const char* terminal = getenv("TERMINAL"); terminal && *terminal) {
        settings.setTerminal(StringView(terminal));
    }

#ifdef IMWAY_FOR_TESTS
    applySettingsFromEnvironment(settings);
#endif
}
