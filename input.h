#pragma once

struct Composer;

// the evdev input devices; their acceleration and the rest of their
// configuration follow the settings, not calls on this object
struct InputSource {
    static InputSource* createLibinput(Composer& c);
};
