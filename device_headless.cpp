#include "composer.h"
#include "device.h"

#include "device_vk.h"
#include "frame_listener.h"
#include "output.h"
#include "renderer.h"
#include "scene.h"
#include "session.h"
#include "util.h"

#include <ev.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/kd.h>
#include <libudev.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <std/dbg/verify.h>
#include <std/ios/fs_utils.h>
#include <std/ios/out_fd.h>
#include <std/ios/sys.h>
#include <std/sys/fs.h>
#include <std/sys/fd.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>
#include "device_headless.h"
#include "pooled.h"

#include <stdlib.h>

using namespace stl;

namespace {
    struct HeadlessOutput: public ::Output {
        int w = 0, h = 0;
        double hz = 60.0;
        FrameListener* frameListener = nullptr;

        // a fake hardware cursor plane, opt-in via IMWAY_FAKE_CURSOR_PLANE,
        // so headless exercises the renderer's hw-cursor rasterization path
        // (rasterizeShape) that a real headless output otherwise never hits
        int curCap = 0;

        HeadlessOutput(int width, int height, double refresh);

        int width() const override;
        int height() const override;
        double refresh() const override;
        StringView outputName() const override;
        StringView make() const override;
        StringView model() const override;
        int physicalWidthMm() const override;
        int physicalHeightMm() const override;
        int cursorCapW() const override;
        int cursorCapH() const override;
        void setCursorImage(const u32*) override;
        void setCursorPos(int, int, bool) override;
        void setPowerSave(bool) override;

        bool hasBrightness() const override;
        float brightness() const override;
        void setBrightness(float) override;
        bool isHdr() const override;
        double sdrWhiteNits() const override;
        void setSdrWhite(double) override;
        void setColorTemp(double) override;
        bool lastFlip(u64&, u32&) const override;
        void setFrameListener(FrameListener*) override;
        bool start() override;
        bool ready() const override;
        bool vsynced() const override;
        int scanoutCount() const override;
        ScanoutBuffer* scanoutBuffer(int) override;
        int acquire() override;
        bool supportsRenderFence() const override;
        void presentImage(int, int) override;
        bool presentNeedsPixels() const override;
        void present(const void*) override;
        bool directScanout(DmabufBuffer*) override;
        void dropScanoutFb(DmabufBuffer*) override;
    };

    struct HeadlessDevice: public Device {
        ObjPool* pool = nullptr;
        struct ev_loop* loop = nullptr;
        DeviceVk* vk = nullptr;
        Vector<DmabufFormat> formats;
        int syncFd = -1;

        HeadlessDevice(ObjPool* p, struct ev_loop* evLoop);
        int drmFd() const override;
        bool explicitSyncSupported() const override;
        unsigned long long renderDevice() const override;
        void dmabufFormatsImpl(VisitorFace&& vis) override;
        ::Output* createOutput(StringView, StringView modeStr, double hdrNits) override;
        Renderer* createRenderer(Composer& c, StringView fontPath, float uiScale, int framesLimit) override;
    };
}

HeadlessOutput::HeadlessOutput(int width, int height, double refresh)
    : w(width)
    , h(height)
    , hz(refresh)
{
    if (getenv("IMWAY_FAKE_CURSOR_PLANE")) {
        curCap = 64;
    }
}

int HeadlessOutput::width() const {
    return w;
}

int HeadlessOutput::height() const {
    return h;
}

double HeadlessOutput::refresh() const {
    return hz;
}

StringView HeadlessOutput::outputName() const { return "HEADLESS-1"_sv; }
StringView HeadlessOutput::make() const { return "imway"_sv; }
StringView HeadlessOutput::model() const { return "headless"_sv; }
int HeadlessOutput::physicalWidthMm() const { return 0; }
int HeadlessOutput::physicalHeightMm() const { return 0; }

int HeadlessOutput::cursorCapW() const {
    return 0;
}

int HeadlessOutput::cursorCapH() const {
    return 0;
}

void HeadlessOutput::setCursorImage(const u32*) {
}

void HeadlessOutput::setCursorPos(int, int, bool) {
}

bool HeadlessOutput::hasBrightness() const {
    return false;
}

float HeadlessOutput::brightness() const {
    return 0.f;
}

void HeadlessOutput::setBrightness(float) {
}

void HeadlessOutput::setPowerSave(bool) {
}

bool HeadlessOutput::isHdr() const {
    return false;
}

double HeadlessOutput::sdrWhiteNits() const {
    return 0;
}

void HeadlessOutput::setSdrWhite(double) {
}

void HeadlessOutput::setColorTemp(double) {
}

bool HeadlessOutput::lastFlip(u64&, u32&) const {
    return false;
}

void HeadlessOutput::setFrameListener(FrameListener* listener) {
    frameListener = listener;
}

bool HeadlessOutput::start() {
    return true;
}

bool HeadlessOutput::ready() const {
    return true;
}

bool HeadlessOutput::vsynced() const {
    return false;
}

int HeadlessOutput::scanoutCount() const {
    return 0;
}

ScanoutBuffer* HeadlessOutput::scanoutBuffer(int) {
    return nullptr;
}

int HeadlessOutput::acquire() {
    return -1;
}

bool HeadlessOutput::supportsRenderFence() const {
    return false;
}

void HeadlessOutput::presentImage(int, int) {
}

bool HeadlessOutput::presentNeedsPixels() const {
    return false;
}

void HeadlessOutput::present(const void*) {
    if (frameListener) {
        frameListener->frameShown(nowMsec());
    }
}

HeadlessDevice::HeadlessDevice(ObjPool* p, struct ev_loop* evLoop)
    : pool(p)
    , loop(evLoop)
{
    vk = pool->make<DeviceVk>(-1);

    if (vk->hasDmabuf) {
        vk->queryDmabufFormats([this](const DmabufFormat& f) { formats.pushBack(f); });

        // a render node is enough for syncobj ioctls (explicit sync)
        for (int i = 128; i < 136 && syncFd < 0; i++) {
            auto& pth = sb();

            pth << "/dev/dri/renderD"_sv << i;

            int fd = open(pth.cStr(), O_RDWR | O_CLOEXEC);

            if (fd < 0) {
                continue;
            }

            u64 cap = 0;

            if (drmGetCap(fd, DRM_CAP_SYNCOBJ_TIMELINE, &cap) == 0 && cap) {
                syncFd = fd;
                vk->drmFd = fd;
                pooledFD(*pool, fd);
            } else {
                close(fd);
            }
        }
    }
}

int HeadlessDevice::drmFd() const {
    return syncFd;
}

bool HeadlessDevice::explicitSyncSupported() const {
    return syncFd >= 0 && vk->hasSyncFd;
}

unsigned long long HeadlessDevice::renderDevice() const {
    return vk->renderDev;
}

void HeadlessDevice::dmabufFormatsImpl(VisitorFace&& vis) {
    for (const DmabufFormat& f : formats) {
        vis.visit((void*)&f);
    }
}

::Output* HeadlessDevice::createOutput(StringView, StringView modeStr, double) {
    ModeSpec m{1280, 800, 60};

    if (!modeStr.empty()) {
        STD_VERIFY(m.parse(modeStr));
    }

    return pool->make<HeadlessOutput>(m.w, m.h, m.hz > 0 ? m.hz : 60.0);
}

Renderer* HeadlessDevice::createRenderer(Composer& c, StringView fontPath, float uiScale, int framesLimit) {
    return Renderer::create(c, *vk, fontPath, uiScale, framesLimit);
}

Device* DeviceHeadless::create(ObjPool* pool, struct ev_loop* loop) {
    return pool->make<HeadlessDevice>(pool, loop);
}

bool HeadlessOutput::directScanout(DmabufBuffer*) {
    return false;
}

void HeadlessOutput::dropScanoutFb(DmabufBuffer*) {
}
