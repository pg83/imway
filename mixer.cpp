#include "mixer.h"
#include "mixer_sndio.h"
#include "mixer_pulse.h"

Mixer* Mixer::create(Composer& c) {
    if (Mixer* m = MixerSndio::create(c)) {
        return m;
    }

    if (Mixer* m = MixerPulse::create(c)) {
        return m;
    }

    return nullptr;
}
