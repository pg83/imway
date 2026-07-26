#include "composer.h"
#include "listener.h"

#include <std/mem/obj_pool.h>

using namespace stl;

// This regression client links Composer without the UI implementation. Runtime
// initialization is irrelevant here; generated defaults are what it verifies.
void applySettingsEnvironment(Settings&) {
}

namespace {
    struct Probe: Listener {
        Settings* settings = nullptr;
        int calls = 0;
        float observed = 0.f;
        bool remove = false;

        void onListen(void*) override {
            calls++;
            observed = settings->uiScale();

            if (remove) {
                unlink();
            }
        }
    };

    struct CountProbe: Listener {
        int calls = 0;

        void onListen(void*) override {
            calls++;
        }
    };
}

int main() {
    ObjPool::Ref pool = ObjPool::fromMemory();
    Composer& composer = *pool->make<Composer>(pool.mutPtr());
    Settings& settings = *composer.settings;
    Probe persistent;
    Probe oneShot;
    CountProbe repeat;
    CountProbe text;
    CountProbe firstShortcut;
    CountProbe secondShortcut;

    persistent.settings = &settings;
    oneShot.settings = &settings;
    oneShot.remove = true;
    settings.addUiScaleListener(&persistent);
    settings.addUiScaleListener(&oneShot);
    settings.addRepeatRateListener(&repeat);

    settings.setUiScale(1.f);

    if (persistent.calls || oneShot.calls || repeat.calls) {
        return 1;
    }

    settings.setUiScale(2.f);

    if (persistent.calls != 1 || oneShot.calls != 1
        || persistent.observed != 2.f || oneShot.observed != 2.f
        || repeat.calls) {
        return 2;
    }

    settings.setUiScale(3.f);

    if (persistent.calls != 2 || oneShot.calls != 1
        || persistent.observed != 3.f || repeat.calls) {
        return 3;
    }

    settings.setUiScale(9.f);

    if (settings.uiScale() != 3.f || persistent.calls != 2) {
        return 4;
    }

    settings.addTerminalListener(&text);

    if (settings.terminal() != StringView("zutty")) {
        return 5;
    }

    settings.setTerminal(
        StringView("a text value longer than the old fixed buffer"));

    if (settings.terminal()
            != StringView("a text value longer than the old fixed buffer")
        || text.calls != 1) {
        return 6;
    }

    settings.setTerminal(
        StringView("a text value longer than the old fixed buffer"));

    if (text.calls != 1) {
        return 7;
    }

    settings.setTerminal({});

    if (!settings.terminal().empty() || text.calls != 2) {
        return 8;
    }

    settings.addShortcutListener(0, &firstShortcut);
    settings.addShortcutListener(1, &secondShortcut);
    ShortcutBinding binding = settings.shortcut(0);

    binding.keysym++;
    settings.setShortcut(0, binding);

    if (firstShortcut.calls != 1 || secondShortcut.calls
        || settings.shortcut(0).keysym != binding.keysym
        || settings.shortcut(1).action != ShortcutAction::lock) {
        return 9;
    }

    return 0;
}
