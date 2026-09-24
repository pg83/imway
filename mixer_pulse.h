#pragma once

struct Composer;
struct Mixer;

// pulseaudio provider (also drives pipewire through its pulse-server
// compatibility). compiled out to a nullptr stub when the libpulse headers
// are absent — see __has_include in the .cpp
struct MixerPulse {
    // nullptr when libpulse is missing or no server answers
    static Mixer* create(Composer& c);
#ifdef IMWAY_FOR_TESTS
    // the pa_mainloop_api contract on a fresh copy of the glue, the parts
    // libpulse's client code does not lean on included; the broken ones
    // counted, -1 without libpulse
    static int mainloopConformance(Composer& c);
#endif
};
