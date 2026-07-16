#pragma once

struct Composer;
struct Mixer;

// sndio provider: server-level output.level/output.mute over sndiod's
// sioctl api, driven on the libev loop
struct MixerSndio {
    // nullptr when sndiod is unreachable
    static Mixer* create(Composer& c);
};
