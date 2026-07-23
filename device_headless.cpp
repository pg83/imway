#include "composer.h"
#include "log.h"
#include "device.h"

#include "device_vk.h"
#include "frame_listener.h"
#include "intr_list.h"
#include "listener.h"
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
#include "pooled_fd.h"

#include <stdlib.h>

using namespace stl;

namespace {
    struct HeadlessOutput: public ::Output {
        Composer* c = nullptr;
        int w = 0, h = 0;
        double hz = 60.0;
        OutputColorState color;
        HdrOutputMetadata metadata;
        double tempK = 0;

        // a fake hardware cursor plane, opt-in via IMWAY_FAKE_CURSOR_PLANE,
        // so headless exercises the renderer's hw-cursor rasterization path
        // (rasterizeShape) that a real headless output otherwise never hits
        int curCap = 0;

        HeadlessOutput(Composer& comp, int width, int height, double refresh,
                       const OutputConfiguration& config);

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
        const OutputColorState& colorState() const override;
        void setSdrWhite(double) override;
        const HdrOutputMetadata& hdrMetadata() const override;
        void setHdrMetadata(const HdrOutputMetadata&) override;
        void setColorTemp(double) override;
        double colorTemp() const override;
        bool lastFlip(u64&, u32&) const override;
        bool start() override;
        bool ready() const override;
        bool vsynced() const override;
        int scanoutCount() const override;
        ScanoutBuffer* scanoutBuffer(int) override;
        int acquire() override;
        bool supportsRenderFence() const override;
        bool presentImage(int, int) override;
        bool prepareScreenshot(Listener&) override;
        bool takeScreenshot(int, SharedScanout&) override;
        bool screenshotPending() const override;
        bool presentNeedsPixels() const override;
        void present(const void*) override;
        bool directScanout(DmabufBuffer*, FrameResource*) override;
        void dropScanoutFb(DmabufBuffer*) override;
        void scanoutFormatsImpl(stl::VisitorFace&&) override;
        void setTearingHint(bool) override {}
    };

    struct HeadlessDevice: public Device {
        Composer* c = nullptr;
        ObjPool* pool = nullptr;
        struct ev_loop* loop = nullptr;
        DeviceVk* vk = nullptr;
        Vector<DmabufFormat> formats;
        int syncFd = -1;

        HeadlessDevice(Composer& comp);
        int drmFd() const override;
        bool explicitSyncSupported() const override;
        unsigned long long renderDevice() const override;
        void dmabufFormatsImpl(VisitorFace&& vis) override;
        void leaseConnectorsImpl(VisitorFace&&) override {}
        int createLease(const u32*, int, u32&) override { return -ENODEV; }
        void revokeLease(u32) override {}
        ::Output* createOutput(StringView, StringView modeStr,
                               const OutputConfiguration& config) override;
        Renderer* createRenderer(Composer& c, StringView fontPath, float uiScale, int framesLimit) override;
    };
}

HeadlessOutput::HeadlessOutput(Composer& comp, int width, int height, double refresh,
                               const OutputConfiguration& config)
    : c(&comp)
    , w(width)
    , h(height)
    , hz(refresh)
    , color(outputColorState(config, {}))
{
    HdrContentMetadata content;

    content.add(ColorDescription::sRgb(), color.sdrWhiteNits);
    metadata = hdrOutputMetadata(color, content);

#ifdef IMWAY_FOR_TESTS
    // headless has no cursor plane; scenarios opt into a fake one to test
    // the hardware-cursor path
    if (getenv("IMWAY_FAKE_CURSOR_PLANE")) {
        curCap = 64;
    }
#endif
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
    return curCap;
}

int HeadlessOutput::cursorCapH() const {
    return curCap;
}

void HeadlessOutput::setCursorImage(const u32* argb) {
#ifdef IMWAY_FOR_TESTS
    if (getenv("IMWAY_DEBUG_CURSOR") && curCap) {
        size_t visible = 0;
        u32 rgbOr = 0;
        u32 alphaOr = 0;

        for (int i = 0; i < curCap * curCap; i++) {
            visible += (argb[i] >> 24) != 0;
            rgbOr |= argb[i] & 0x00ffffff;
            alphaOr |= argb[i] >> 24;
        }

        *(c->log) << "cursor image: visible "_sv << visible << ", rgb "_sv << rgbOr << ", alpha "_sv << alphaOr << endL;
    }
#else
    (void)argb;
#endif
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

const OutputColorState& HeadlessOutput::colorState() const {
    return color;
}

void HeadlessOutput::setSdrWhite(double nits) {
    if (color.hdr() && nits > 0 && nits != color.sdrWhiteNits) {
        color.setSdrWhite(nits);
        c->scene->needsFrame = true;
    }
}

const HdrOutputMetadata& HeadlessOutput::hdrMetadata() const {
    return metadata;
}

void HeadlessOutput::setHdrMetadata(const HdrOutputMetadata& value) {
    metadata = value;
}

void HeadlessOutput::setColorTemp(double kelvin) {
    tempK = kelvin > 0 && kelvin < 6500 ? kelvin : 0;
    c->scene->needsFrame = true;
}

double HeadlessOutput::colorTemp() const {
    return tempK;
}

bool HeadlessOutput::lastFlip(u64&, u32&) const {
    return false;
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

bool HeadlessOutput::presentImage(int, int) {
    return false;
}

bool HeadlessOutput::prepareScreenshot(Listener&) {
    return false;
}

bool HeadlessOutput::takeScreenshot(int, SharedScanout&) {
    return false;
}

bool HeadlessOutput::screenshotPending() const {
    return false;
}

bool HeadlessOutput::presentNeedsPixels() const {
    return false;
}

void HeadlessOutput::present(const void*) {
    u32 msec = nowMsec();

    FrameEvent event{msec};

    forEach<Listener>(c->frameListeners, [&event](Listener& listener) {
        listener.onListen(&event);
    });
}

HeadlessDevice::HeadlessDevice(Composer& comp)
    : c(&comp)
    , pool(comp.pool)
    , loop(comp.loop)
{
    vk = pool->make<DeviceVk>(*c->log, -1);

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

::Output* HeadlessDevice::createOutput(StringView, StringView modeStr,
                                       const OutputConfiguration& config) {
    ModeSpec m{1280, 800, 60};

    if (!modeStr.empty()) {
        STD_VERIFY(m.parse(modeStr));
    }

    return pool->make<HeadlessOutput>(*c, m.w, m.h, m.hz > 0 ? m.hz : 60.0, config);
}

Renderer* HeadlessDevice::createRenderer(Composer& c, StringView fontPath, float uiScale, int framesLimit) {
    return Renderer::create(c, *vk, fontPath, uiScale, framesLimit);
}

Device* DeviceHeadless::create(Composer& c) {
    return c.pool->make<HeadlessDevice>(c);
}

bool HeadlessOutput::directScanout(DmabufBuffer*, FrameResource*) {
    return false;
}

void HeadlessOutput::dropScanoutFb(DmabufBuffer*) {
}

void HeadlessOutput::scanoutFormatsImpl(stl::VisitorFace&&) {
}
