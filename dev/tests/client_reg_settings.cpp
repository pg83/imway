#include "settings.h"

#include "listener.h"

namespace {
    struct Probe: Listener {
        Setting<int>* setting = nullptr;
        int calls = 0;
        int observed = 0;
        bool remove = false;

        void onListen(void*) override {
            calls++;
            observed = setting->get();

            if (remove) {
                unlink();
            }
        }
    };

    struct TextProbe: Listener {
        int calls = 0;

        void onListen(void*) override {
            calls++;
        }
    };
}

int main() {
    Setting<int> first{7};
    Setting<int> second{11};
    Probe persistent;
    Probe oneShot;
    Probe other;

    persistent.setting = &first;
    oneShot.setting = &first;
    oneShot.remove = true;
    other.setting = &second;

    first.changedListeners.pushBack(&persistent);
    first.changedListeners.pushBack(&oneShot);
    second.changedListeners.pushBack(&other);

    if (first.set(7) || persistent.calls || oneShot.calls || other.calls) {
        return 1;
    }

    if (!first.set(9) || persistent.calls != 1 || oneShot.calls != 1 || persistent.observed != 9 || oneShot.observed != 9 || other.calls) {
        return 2;
    }

    if (!first.set(12) || persistent.calls != 2 || oneShot.calls != 1 || persistent.observed != 12 || other.calls) {
        return 3;
    }

    TextSetting<5> text;
    TextProbe textProbe;

    text.changedListeners.pushBack(&textProbe);

    if (!text.set(stl::StringView("abcdef")) || text.get() != stl::StringView("abcd") || textProbe.calls != 1) {
        return 4;
    }

    // The discarded suffix is outside the canonical value and must not
    // generate another change event.
    if (text.set(stl::StringView("abcdZZ")) || textProbe.calls != 1) {
        return 5;
    }

    return 0;
}
