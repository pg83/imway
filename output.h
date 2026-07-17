#pragma once

#include <std/sys/types.h>
#include <std/str/view.h>

struct ScanoutBuffer;
struct DmabufBuffer;
struct FrameListener;

struct Output {
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double refresh() const = 0;
    virtual stl::StringView outputName() const = 0;
    virtual stl::StringView make() const = 0;
    virtual stl::StringView model() const = 0;
    virtual int physicalWidthMm() const = 0;
    virtual int physicalHeightMm() const = 0;

    // hardware cursor plane; the image is capW x capH premultiplied ARGB8888,
    // cap of 0 means no hardware cursor on this output
    virtual int cursorCapW() const = 0;
    virtual int cursorCapH() const = 0;
    virtual void setCursorImage(const u32* argb) = 0;
    virtual void setCursorPos(int x, int y, bool visible) = 0;

    // false turns the display off (DPMS), true brings it back
    virtual void setPowerSave(bool on) = 0;

    // physical panel brightness 0..1 — the sysfs backlight matched to the
    // connector. multiplies everything including hdr, unlike the sdr-white
    // knob which rescales the signal; absent on outputs without a panel
    virtual bool hasBrightness() const = 0;
    virtual float brightness() const = 0;
    virtual void setBrightness(float v) = 0;

    // macOS-style "sdr white": brightness of SDR 1.0 on the HDR pipeline in
    // nits, 0 means the hdr path is off; setting takes effect on the next
    // frame commit via a GAMMA_LUT rebuild
    virtual bool isHdr() const = 0;
    virtual double sdrWhiteNits() const = 0;
    virtual void setSdrWhite(double nits) = 0;

    // night light: color temperature in kelvin, <= 0 or >= 6500 is neutral;
    // rides the GAMMA_LUT like the sdr white knob, works without hdr too
    virtual void setColorTemp(double kelvin) = 0;

    // timestamp (CLOCK_MONOTONIC ns) and vblank sequence of the last
    // completed pageflip; false when the backend has no real flips
    virtual bool lastFlip(u64& nsec, u32& seq) const = 0;
    virtual void setFrameListener(FrameListener* listener) = 0;

    virtual bool start() = 0;

    virtual bool ready() const = 0;
    virtual bool vsynced() const = 0;

    virtual int scanoutCount() const = 0;
    virtual ScanoutBuffer* scanoutBuffer(int i) = 0;
    virtual int acquire() = 0;
    // True when the backend can attach a Vulkan completion sync_file to the
    // primary-plane commit.  The fd passed to presentImage is borrowed and
    // remains owned by the caller.
    virtual bool supportsRenderFence() const = 0;
    virtual void presentImage(int i, int renderFenceFd) = 0;

    virtual bool presentNeedsPixels() const = 0;
    virtual void present(const void* pixels) = 0;

    // fullscreen bypass: scan a client dmabuf out directly on the primary
    // plane, skipping composition; false when it cannot be imported or
    // committed (wrong format, busy). dropScanoutFb releases the imported
    // drm fb when the client buffer dies
    virtual bool directScanout(DmabufBuffer* buf) = 0;
    virtual void dropScanoutFb(DmabufBuffer* buf) = 0;
};
