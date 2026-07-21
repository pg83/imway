#pragma once

#include "color.h"
#include "frame_resource.h"
#include "visitor.h"

#include <std/sys/types.h>
#include <std/str/view.h>

struct ScanoutBuffer;
struct DmabufBuffer;
struct DmabufFormat;
struct Listener;

struct SharedScanout {
    int fd = -1;
    u32 width = 0;
    u32 height = 0;
    u32 format = 0;
    u32 offset = 0;
    u32 stride = 0;
    u64 modifier = 0;
    u64 allocationSize = 0;
    u64 renderDevice = 0;
    OutputColorState color;
};

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

    // Physical panel brightness 0..1 — the sysfs/DDC backlight matched to the
    // connector. The renderer uses it only for SDR: changing it under HDR
    // would invalidate the calibrated absolute PQ luminance and headroom.
    virtual bool hasBrightness() const = 0;
    virtual float brightness() const = 0;
    virtual void setBrightness(float v) = 0;

    // macOS-style "sdr white": brightness of SDR 1.0 on the HDR pipeline in
    // nits, 0 means the hdr path is off; HDR client luminance stays absolute
    virtual const OutputColorState& colorState() const = 0;
    virtual void setSdrWhite(double nits) = 0;
    virtual const HdrOutputMetadata& hdrMetadata() const = 0;
    virtual void setHdrMetadata(const HdrOutputMetadata& metadata) = 0;

    // night light: color temperature in kelvin, <= 0 or >= 6500 is neutral
    virtual void setColorTemp(double kelvin) = 0;
    virtual double colorTemp() const = 0;

    // timestamp (CLOCK_MONOTONIC ns) and vblank sequence of the last
    // completed pageflip; false when the backend has no real flips
    virtual bool lastFlip(u64& nsec, u32& seq) const = 0;
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
    virtual bool presentImage(int i, int renderFenceFd) = 0;

    // Build a replacement scanout away from the event loop, then transfer
    // ownership of one rendered slot to a screenshot client.  The backend
    // retires its handles after KMS has flipped away from that slot.
    virtual bool prepareScreenshot(Listener& ready) = 0;
    virtual bool takeScreenshot(int i, SharedScanout& image) = 0;
    virtual bool screenshotPending() const = 0;

    virtual bool presentNeedsPixels() const = 0;
    virtual void present(const void* pixels) = 0;

    // fullscreen bypass: scan a client dmabuf out directly on the primary
    // plane, skipping composition; false when it cannot be imported or
    // committed (wrong format, busy). dropScanoutFb releases the imported
    // drm fb when the client buffer dies
    virtual bool directScanout(DmabufBuffer* buf, FrameResource* frame) = 0;
    virtual void dropScanoutFb(DmabufBuffer* buf) = 0;

    // wp-tearing-control: allow an async (immediate) page flip for the next
    // direct-scanout present. Best-effort; ignored without hardware support.
    virtual void setTearingHint(bool allow) = 0;

    // format+modifier pairs the primary plane can scan out; feeds the dmabuf
    // feedback scanout tranche so clients allocate bypass-capable buffers
    virtual void scanoutFormatsImpl(stl::VisitorFace&& vis) = 0;

    template <typename F>
    void scanoutFormats(F f) {
        scanoutFormatsImpl(visitEach<DmabufFormat>(f));
    }
};
