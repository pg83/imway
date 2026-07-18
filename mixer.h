#pragma once

struct Composer;

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
