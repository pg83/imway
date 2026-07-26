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

    TextSetting text;
    TextProbe textProbe;

    text.changedListeners.pushBack(&textProbe);

    if (!text.get().empty() || !text.set(stl::StringView("a text value longer than the old fixed buffer")) || text.get() != stl::StringView("a text value longer than the old fixed buffer") || textProbe.calls != 1) {
        return 4;
    }

    if (text.set(stl::StringView("a text value longer than the old fixed buffer")) || textProbe.calls != 1) {
        return 5;
    }

    if (!text.set(stl::StringView()) || !text.get().empty() || textProbe.calls != 2) {
        return 6;
    }

    if (text.set(stl::StringView()) || textProbe.calls != 2) {
        return 7;
    }

    TextSetting withDefault{"default"};

    if (withDefault.get() != stl::StringView("default")) {
        return 8;
    }

    return 0;
}
