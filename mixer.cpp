#include "mixer.h"

#include "composer.h"
#include "mixer_pulse.h"
#include "mixer_sndio.h"

Mixer* Mixer::create(Composer& c) {
    BackendPreference preference = c.settings.audioBackend.get();

    if (preference == BackendPreference::disabled) {
        return nullptr;
    }

    if (preference != BackendPreference::second) {
        if (Mixer* m = MixerSndio::create(c)) {
            return m;
        }
    }

    if (preference != BackendPreference::first) {
        if (Mixer* m = MixerPulse::create(c)) {
            return m;
        }
    }

    return nullptr;
}
