#pragma once

#include <std/sys/types.h>

struct ScanoutBuffer;

struct Output {
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double refresh() const = 0;

    // hardware cursor plane; the image is capW x capH premultiplied ARGB8888,
    // cap of 0 means no hardware cursor on this output
    virtual int cursorCapW() const = 0;
    virtual int cursorCapH() const = 0;
    virtual void setCursorImage(const u32* argb) = 0;
    virtual void setCursorPos(int x, int y, bool visible) = 0;

    // false turns the display off (DPMS), true brings it back
    virtual void setPowerSave(bool on) = 0;

    // macOS-style "sdr white": brightness of SDR 1.0 on the HDR pipeline in
    // nits, 0 means the hdr path is off; setting takes effect on the next
    // frame commit via a GAMMA_LUT rebuild
    virtual double sdrWhiteNits() const = 0;
    virtual void setSdrWhite(double nits) = 0;

    virtual bool start() = 0;

    virtual bool ready() const = 0;
    virtual bool vsynced() const = 0;

    virtual int scanoutCount() const = 0;
    virtual ScanoutBuffer* scanoutBuffer(int i) = 0;
    virtual int acquire() = 0;
    virtual void presentImage(int i) = 0;

    virtual bool presentNeedsPixels() const = 0;
    virtual void present(const void* pixels) = 0;
};
