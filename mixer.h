#pragma once

struct Composer;

// any volume change — media keys, the settings slider, an external mixer —
// lands here; the osd is the subscriber
struct MixerListener {
    virtual void volumeChanged() = 0;
};

// audio output volume, 0..1; provider-agnostic
struct Mixer {
    virtual float volume() = 0;
    virtual void setVolume(float v) = 0;
    virtual bool muted() = 0;
    virtual void setMuted(bool m) = 0;

    // tries the providers in priority order (sndio, then pulse — which also
    // covers pipewire via pipewire-pulse) and returns the first that comes
    // up, or nullptr if none is reachable
    static Mixer* create(Composer& c);
};
