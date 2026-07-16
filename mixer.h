#pragma once

// any volume change — media keys, the settings slider, an external
// sndioctl — lands here; the osd is the subscriber
struct MixerListener {
    virtual void volumeChanged() = 0;
};

// audio output volume, 0..1; the sndio provider today, anything speaking
// the same four calls tomorrow
struct Mixer {
    virtual float volume() = 0;
    virtual void setVolume(float v) = 0;
    virtual bool muted() = 0;
    virtual void setMuted(bool m) = 0;
};
