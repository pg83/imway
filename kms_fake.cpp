#include "kms_fake.h"

#include "log.h"
#include "util.h"
#include "kms_intercept.h"

#include <std/ios/sys.h>
#include <std/str/view.h>
#include <std/sys/types.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <xf86drm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <drm_fourcc.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <xf86drmMode.h>
#include <linux/i2c-dev.h>

// Everything KMS that device_kms.cpp needs, modeled in userspace: the
// object/property tables, atomic commits with page-flip events on a real
// pipe, framebuffer and GEM bookkeeping. Rendering stays on the real GPU;
// syncobj ioctls forward to a companion render node so explicit sync is
// backed by genuine kernel fences. The point is a scriptable state machine:
// tests can flip the connector or make commits fail on demand.

using namespace stl;

namespace {
    constexpr u32 kConnectorId = 101;
    constexpr u32 kEncoderId = 102;
    constexpr u32 kCrtcId = 103;
    constexpr u32 kPlaneId = 104;
    constexpr u32 kCursorPlaneId = 105;

    // a second pipe the compositor never drives: its connector carries
    // "non-desktop", so it is the one offered over wp-drm-lease
    constexpr u32 kLeaseConnectorId = 301;
    constexpr u32 kLeaseEncoderId = 302;
    constexpr u32 kLeaseCrtcId = 303;
    // the second crtc's own primary plane, listed first: the desktop pipe
    // walks past it, a lease picks it up
    constexpr u32 kLeasePlaneId = 304;
    // the longest mode list a connector offers
    constexpr u32 kMaxModes = 40;

    // property ids, one flat namespace across objects
    enum : u32 {
        pConnCrtcId = 201,
        pConnColorspace,
        pConnMaxBpc,
        pConnBroadcastRgb,
        pConnHdrMeta,
        pConnEdid,
        pConnLinkBpc,
        pConnNonDesktop,
        pCrtcModeId,
        pCrtcActive,
        pCrtcGamma,
        pCrtcDegamma,
        pCrtcCtm,
        pPlaneType,
        pPlaneFbId,
        pPlaneCrtcId,
        pPlaneInFenceFd,
        pPlaneSrcX,
        pPlaneSrcY,
        pPlaneSrcW,
        pPlaneSrcH,
        pPlaneCrtcX,
        pPlaneCrtcY,
        pPlaneCrtcW,
        pPlaneCrtcH,
        pPlaneInFormats,
        pCursorType,
        pCursorFbId,
        pCursorCrtcId,
        pCursorSrcX,
        pCursorSrcY,
        pCursorSrcW,
        pCursorSrcH,
        pCursorCrtcX,
        pCursorCrtcY,
        pCursorCrtcW,
        pCursorCrtcH,
        pLeasePlaneType,
    };

    struct PropEnum {
        u64 value;
        const char* name;
    };

    struct PropDef {
        u32 id = 0;
        u32 obj = 0;
        const char* name = nullptr;
        u32 flags = 0;
        const PropEnum* enums = nullptr;
        int enumCount = 0;
        u64 rangeMin = 0, rangeMax = 0;
        u64 value = 0;
    };

    const PropEnum kColorspaceEnums[] = {
        {0, "Default"},
        {1, "BT2020_RGB"},
        {2, "BT2020_YCC"},
    };

    const PropEnum kBroadcastEnums[] = {
        {0, "Automatic"},
        {1, "Full"},
        {2, "Limited 16:235"},
    };

    // older drivers spell the limited range without the code values
    const PropEnum kBroadcastLegacyEnums[] = {
        {0, "Automatic"},
        {1, "Full"},
        {2, "Limited"},
    };

    const PropEnum kTypeEnums[] = {
        {DRM_PLANE_TYPE_PRIMARY, "Primary"},
        {DRM_PLANE_TYPE_CURSOR, "Cursor"},
        {DRM_PLANE_TYPE_OVERLAY, "Overlay"},
    };

    const u32 kFormats[] = {
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_ARGB8888,
        DRM_FORMAT_XRGB2101010,
        DRM_FORMAT_ARGB2101010,
        DRM_FORMAT_XBGR2101010,
        DRM_FORMAT_ABGR2101010,
        DRM_FORMAT_ABGR16161616F,
        DRM_FORMAT_XBGR16161616F,
    };

    // heap-boxed: swap-remove must not move the byte vector
    struct FakeBlob {
        u32 id = 0;
        Vector<u8> data;

        FakeBlob() = default;
        FakeBlob(const FakeBlob&) = delete;
    };

    struct FakeFb {
        u32 id = 0;
        u32 width = 0, height = 0, format = 0;
        u64 modifier = 0;
    };

    struct FakeGem {
        u32 handle = 0;
        int fd = -1; // kept dup of the imported dmabuf, or the dumb memfd
        int refs = 0;
        u64 dumbSize = 0; // nonzero marks a dumb buffer
    };

    // a lookup the driver fails: which ioctl, on what, after how many that
    // pass, for how many more (-1: for good)
    struct LookupFault {
        u32 req = 0;
        u32 id = 0;
        char name[32] = {};
        int skip = 0;
        int count = 0;
    };

    struct FakeKms: KmsIntercept {
        int clientFd = -1; // handed to the compositor; events are read here
        int eventFd = -1;  // emulator's write end
        int renderFd = -1; // companion real node: syncobjs, identity
        dev_t renderDev = 0;

        pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
        pthread_t flipThread{};

        int connected = 1; // 0 unplugged, 1 plugged, 2 connector gone
        int modeSet = 0; // 0 default, 1 tv, 2 small, 3 1366x768 panel, 4 none, 5 forty unpreferred
        bool noPrime = false;
        bool asyncFlipLogged = false;
        bool cursorOnLogged = false;
        bool flipsHeld = false;
        Vector<PropDef> props;
        Vector<FakeBlob*> blobs;
        Vector<FakeFb> fbs;
        Vector<FakeGem> gems;
        u32 nextBlob = 1000;
        u32 nextFb = 2000;
        u32 nextHandle = 1;

        bool flipPending = false;
        u64 flipUserData = 0;
        u64 flipDueNs = 0;
        u32 flipSeq = 0;

        // fault injection
        int failErr = 0;
        int failCount = 0;
        bool failTestToo = false;
        int failNewFbErr = 0;
        u32 failNewFbSince = 0;
        int failPrimeErr = 0;
        int failPrimeCount = 0;
        int failPrimeSkip = 0;
        int failAddFbErr = 0;
        int failAddFbCount = 0;
        int failAddFbSkip = 0;
        int rejectCursorErr = 0;
        bool rejectColor = false;
        bool internalPanel = false;

        // the device's shape at boot, read from IMWAY_FAKE_KMS_* by
        // openDevice: a scenario boots a different display or driver
        StringView dropProps; // property names the driver does not expose
        StringView zeroProps; // exposed, but reading 0 (no blob behind it)
        int edidKind = 0;     // 0 hdr, 1 sdr, 2 unparseable, 3 no BT.2020 RGB
        u64 minBpcCap = 6;
        u64 maxBpcCap = 16;
        bool legacyLimited = false;
        u64 cursorCap = 64;
        bool unbound = false; // cold boot: no encoder or crtc bound yet
        bool noCrtc = false;  // the encoder reaches no crtc at all
        bool no10Bit = false; // the primary plane lacks the 2101010 formats
        bool tiledOnly = false; // the plane scans out no LINEAR buffer
        int failDumbCount = 0;
        int leaseFaultKind = 0;
        Vector<LookupFault> lookupFaults;
        bool noAsync = false;

        // the monitor behind the connector's DDC/CI bus (IMWAY_FAKE_KMS_DDC):
        // absent answers no address, silent never replies, a number is the
        // brightness maximum of one that does. The bus end is a socket; its
        // identity, not its fd number, marks the I2C_SLAVE ioctl as ours,
        // since the number is reused once the compositor lets go of it
        bool ddcArmed = false;
        bool ddcAbsent = false;
        bool ddcSilent = false;
        int ddcMax = 0;
        int ddcCur = 0;
        int ddcPeer = -1;
        ino_t ddcIno = 0;
        dev_t ddcDev = 0;
        pthread_t ddcThread{};

        u64 flipsDone = 0;
        u32 lastLessee = 0;

        int openDevice() override;
        void setConnected(int state) override;
        void setModes(int set) override;
        void failCommits(int err, int count, bool testToo) override;
        void failNewFb(int err) override;
        void failPrime(int err, int count, int skip) override;
        void failAddFb(int err, int count) override;
        void rejectCursor(int err) override;
        void leaseFault(int kind) override;
        void failLookups(StringView rules) override;
        void parseLookupFaults(StringView rules);
        bool lookupFails(u32 req, void* arg);
        int openDdc(StringView bus) override;
        void holdFlips(bool hold) override;
        unsigned long long flips() override;
        int liveFbs() override;
        int liveGems() override;

        PropDef* findProp(u32 id);
        FakeBlob* findBlob(u32 id);
        void addProp(u32 obj, u32 id, const char* name, u32 flags, const PropEnum* enums, int enumCount, u64 mn, u64 mx, u64 value);
        u32 makeEdidBlob();
        void buildProps();
        u32 currentModes(drm_mode_modeinfo* modes);
        int emuGetCap(drm_get_cap* c);
        int emuGetResources(drm_mode_card_res* r);
        int emuGetConnector(drm_mode_get_connector* c);
        int emuGetEncoder(drm_mode_get_encoder* e);
        int emuGetPlaneResources(drm_mode_get_plane_res* r);
        int emuGetPlane(drm_mode_get_plane* p);
        int emuObjGetProperties(drm_mode_obj_get_properties* o);
        int emuGetProperty(drm_mode_get_property* q);
        int emuGetPropBlob(drm_mode_get_blob* b);
        int emuCreateBlob(drm_mode_create_blob* b);
        int emuDestroyBlob(drm_mode_destroy_blob* b);
        int emuPrimeFdToHandle(drm_prime_handle* p);
        int emuGemClose(drm_gem_close* c);
        int emuCreateDumb(drm_mode_create_dumb* c);
        int emuMapDumb(drm_mode_map_dumb* m);
        int emuDestroyDumb(drm_mode_destroy_dumb* d);
        int emuAddFb2(drm_mode_fb_cmd2* f);
        int emuRmFb(u32* id);
        int emuAtomic(drm_mode_atomic* a);
        int emuCreateLease(drm_mode_create_lease* l);
        int emuRevokeLease(drm_mode_revoke_lease* l);
        long fakeIoctl(unsigned long req, void* arg);
        int dumbMemFd(unsigned long long off);
        void flipLoop();
        void ddcLoop();
        bool isDdcBus(int fd);
    };

    FakeKms* g = nullptr;

    // Dumb buffers are memfds: the compositor's cursor plane (and the
    // no-zero-copy fallback path) mmaps them through the interposer below.
    // The map offset is a token, handle-tagged; the real mapping always
    // starts at the memfd's origin.
    constexpr int kDumbOffsetShift = 20;

    u64 nowNs() {
        timespec ts{};

        clock_gettime(CLOCK_MONOTONIC, &ts);

        return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
    }

    long rawIoctl(int fd, unsigned long req, void* arg) {
        long rc = syscall(SYS_ioctl, fd, req, arg);

        return rc;
    }

    void fillMode(drm_mode_modeinfo& m, u16 w, u16 h, u32 hz, bool preferred) {
        memset(&m, 0, sizeof(m));
        m.hdisplay = w;
        m.hsync_start = (u16)(w + 48);
        m.hsync_end = (u16)(w + 80);
        m.htotal = (u16)(w + 160);
        m.vdisplay = h;
        m.vsync_start = (u16)(h + 3);
        m.vsync_end = (u16)(h + 8);
        m.vtotal = (u16)(h + 45);
        m.clock = (u32)((u64)m.htotal * m.vtotal * hz / 1000);
        m.vrefresh = hz;
        m.type = DRM_MODE_TYPE_DRIVER | (preferred ? DRM_MODE_TYPE_PREFERRED : 0);

        char name[32];
        int n = 0;
        u32 v = w;

        // hand-rolled "%ux%u": no snprintf in tree code
        char tmp[16];
        int t = 0;

        do {
            tmp[t++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);

        while (t) {
            name[n++] = tmp[--t];
        }

        name[n++] = 'x';
        v = h;

        do {
            tmp[t++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);

        while (t) {
            name[n++] = tmp[--t];
        }

        name[n] = 0;
        memcpy(m.name, name, (size_t)n + 1);
    }

    // the two-call fill pattern every enumeration ioctl uses: report the
    // count, copy when the caller supplied enough room
    template <typename T>
    void fillArray(u64 ptr, u32& count, const T* src, u32 n) {
        if (ptr && count >= n) {
            memcpy((void*)(uintptr_t)ptr, src, sizeof(T) * n);
        }

        count = n;
    }

    // IN_FORMATS: the header, the format list, then the modifier structs:
    // LINEAR for every format, and a vendor tiling for XRGB8888 alone that
    // no renderer here can produce, so the intersection has to drop it
    void buildInFormatsBlob(Vector<u8>& out, bool no10Bit, bool tiledOnly) {
        Vector<u32> formats;

        for (u32 f : kFormats) {
            bool tenBit = f == DRM_FORMAT_XRGB2101010 || f == DRM_FORMAT_ARGB2101010 || f == DRM_FORMAT_XBGR2101010 || f == DRM_FORMAT_ABGR2101010;

            if (!no10Bit || !tenBit) {
                formats.pushBack(f);
            }
        }

        u32 nFmt = (u32)formats.length();
        struct drm_format_modifier_blob hdr{};

        hdr.version = 1;
        hdr.count_formats = nFmt;
        hdr.formats_offset = sizeof(hdr);
        hdr.count_modifiers = 2;
        hdr.modifiers_offset = (u32)(sizeof(hdr) + sizeof(u32) * nFmt);

        struct drm_format_modifier mods[2]{};

        mods[0].formats = tiledOnly ? 0 : (1ull << nFmt) - 1;
        mods[0].modifier = DRM_FORMAT_MOD_LINEAR;
        mods[1].formats = 1;
        mods[1].modifier = DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED;

        out.append((const u8*)&hdr, sizeof(hdr));
        out.append((const u8*)formats.data(), sizeof(u32) * nFmt);
        out.append((const u8*)mods, sizeof(mods));
    }

    // a comma-separated name list, as the boot knobs spell them
    static bool listed(StringView list, StringView name) {
        while (!list.empty()) {
            StringView item, rest;

            if (list.split(',', item, rest)) {
                list = rest;
            } else {
                item = list;
                list = {};
            }

            if (item == name) {
                return true;
            }
        }

        return false;
    }

    static void* ddcThreadTrampoline(void* self) {
        ((FakeKms*)self)->ddcLoop();

        return nullptr;
    }

    void* flipThreadTrampoline(void* self) {
        ((FakeKms*)self)->flipLoop();

        return nullptr;
    }
}

PropDef* FakeKms::findProp(u32 id) {
    for (size_t i = 0; i < props.length(); i++) {
        if (props[i].id == id) {
            return &props.mut(i);
        }
    }

    return nullptr;
}

FakeBlob* FakeKms::findBlob(u32 id) {
    for (FakeBlob* b : blobs) {
        if (b->id == id) {
            return b;
        }
    }

    return nullptr;
}

void FakeKms::addProp(u32 obj, u32 id, const char* name, u32 flags, const PropEnum* enums, int enumCount, u64 mn, u64 mx, u64 value) {
    // a driver without the property: the lease pipe keeps its own
    if (obj != kLeaseConnectorId && listed(dropProps, StringView(name))) {
        return;
    }

    PropDef p;

    p.id = id;
    p.obj = obj;
    p.name = name;
    p.flags = flags;
    p.enums = enums;
    p.enumCount = enumCount;
    p.rangeMin = mn;
    p.rangeMax = mx;
    p.value = listed(zeroProps, StringView(name)) ? 0 : value;
    props.pushBack(p);
}

// A minimal but structurally valid EDID 1.4 with one CTA-861 extension:
// sRGB chromaticity, a 1280x800 preferred timing, BT.2020 RGB
// colorimetry and PQ HDR static metadata (~1000 nit peak, 400 nit
// maxFALL, 0.1 nit floor). libdisplay-info reads it like a real HDR
// panel's, which opens the compositor's positive HDR path.
u32 FakeKms::makeEdidBlob() {
    u8 e[256];

    memset(e, 0, 256);

    const u8 header[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};

    memcpy(e, header, 8);
    e[8] = 0x19; // "FKE"
    e[9] = 0x65;
    e[16] = 1; // week 1 of 2020
    e[17] = 30;
    e[18] = 1; // EDID 1.4
    e[19] = 4;
    e[20] = 0xb5; // digital input, 10 bpc
    e[21] = 34;   // physical size, cm
    e[22] = 21;
    e[23] = 120;  // gamma 2.2
    e[24] = 0x06; // preferred timing present, sRGB default

    // sRGB primaries + D65 white, 10-bit fixed point
    const u16 chroma[8] = {655, 338, 307, 614, 154, 61, 320, 337};

    e[25] = (u8)(((chroma[0] & 3) << 6) | ((chroma[1] & 3) << 4) | ((chroma[2] & 3) << 2) | (chroma[3] & 3));
    e[26] = (u8)(((chroma[4] & 3) << 6) | ((chroma[5] & 3) << 4) | ((chroma[6] & 3) << 2) | (chroma[7] & 3));

    for (int i = 0; i < 8; i++) {
        e[27 + i] = (u8)(chroma[i] >> 2);
    }

    // preferred detailed timing: 1280x800@60, 83.5 MHz
    const u8 dtd[18] = {0x9e, 0x20, 0x00, 0xa0, 0x50, 0x20, 0x2d, 0x30, 0x30, 0x20, 0x36, 0x00, 0x54, 0xd2, 0x10, 0x00, 0x00, 0x1e};

    memcpy(e + 54, dtd, 18);

    // display name, then two dummy descriptors
    const u8 name[18] = {0, 0, 0, 0xfc, 0, 'F', 'a', 'k', 'e', 'K', 'M', 'S', '\n', ' ', ' ', ' ', ' ', ' '};

    memcpy(e + 72, name, 18);
    e[93] = 0x10;
    e[111] = 0x10;
    e[126] = 1; // one extension block

    // CTA-861 revision 3, data blocks end at byte 15
    u8* c = e + 128;

    c[0] = 0x02;
    c[1] = 0x03;
    c[2] = 15;
    c[3] = 0x00;

    // colorimetry data block: BT.2020 RGB
    c[4] = 0xe3;
    c[5] = 0x05;
    c[6] = 0x80;
    c[7] = 0x00;

    // HDR static metadata: PQ + SDR EOTFs, type 1 descriptor,
    // luminance codes for ~1000 / 400 / 0.1 nits
    c[8] = 0xe6;
    c[9] = 0x06;
    c[10] = 0x05;
    c[11] = 0x01;
    c[12] = 0x8a;
    c[13] = 0x60;
    c[14] = 0x1a;

    // a PQ panel that cannot take BT.2020 RGB signalling
    if (edidKind == 3) {
        c[6] = 0x00;
    }

    // an sdr panel: the base block alone, no colorimetry, no HDR metadata
    int blocks = edidKind == 1 ? 1 : 2;

    if (edidKind == 1) {
        e[126] = 0;
    }

    for (int block = 0; block < blocks; block++) {
        u8 sum = 0;

        for (int i = 0; i < 127; i++) {
            sum = (u8)(sum + e[block * 128 + i]);
        }

        e[block * 128 + 127] = (u8)(0u - sum);
    }

    // a corrupted read over the ddc wire: the fixed header is gone
    if (edidKind == 2) {
        e[0] = 0x12;
    }

    auto* blob = new FakeBlob();

    blob->id = nextBlob++;
    blob->data.append(e, (size_t)blocks * 128);
    blobs.pushBack(blob);

    return blob->id;
}

void FakeKms::buildProps() {
    addProp(kConnectorId, pConnCrtcId, "CRTC_ID", DRM_MODE_PROP_OBJECT, nullptr, 0, 0, 0, 0);
    addProp(kConnectorId, pConnColorspace, "Colorspace", DRM_MODE_PROP_ENUM, kColorspaceEnums, 3, 0, 0, 0);
    addProp(kConnectorId, pConnMaxBpc, "max bpc", DRM_MODE_PROP_RANGE, nullptr, 0, minBpcCap, maxBpcCap, 10);
    addProp(kConnectorId, pConnBroadcastRgb, "Broadcast RGB", DRM_MODE_PROP_ENUM, legacyLimited ? kBroadcastLegacyEnums : kBroadcastEnums, 3, 0, 0, 0);
    addProp(kConnectorId, pConnHdrMeta, "HDR_OUTPUT_METADATA", DRM_MODE_PROP_BLOB, nullptr, 0, 0, 0, 0);
    addProp(kConnectorId, pConnEdid, "EDID", DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE, nullptr, 0, 0, 0, makeEdidBlob());

    // what the link actually negotiated; a scenario boots it low to
    // exercise the HDR degradation ladder
    const char* linkBpc = getenv("IMWAY_FAKE_KMS_LINK_BPC");

    addProp(kConnectorId, pConnNonDesktop, "non-desktop", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_IMMUTABLE, nullptr, 0, 0, 1, 0);
    addProp(kLeaseConnectorId, pConnNonDesktop, "non-desktop", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_IMMUTABLE, nullptr, 0, 0, 1, 1);
    addProp(kLeaseConnectorId, pConnCrtcId, "CRTC_ID", DRM_MODE_PROP_OBJECT, nullptr, 0, 0, 0, 0);
    addProp(kConnectorId, pConnLinkBpc, "link bpc", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_IMMUTABLE, nullptr, 0, 0, 16, linkBpc ? (u64)atoi(linkBpc) : 10);

    addProp(kCrtcId, pCrtcModeId, "MODE_ID", DRM_MODE_PROP_BLOB, nullptr, 0, 0, 0, 0);
    addProp(kCrtcId, pCrtcActive, "ACTIVE", DRM_MODE_PROP_RANGE, nullptr, 0, 0, 1, 0);
    addProp(kCrtcId, pCrtcGamma, "GAMMA_LUT", DRM_MODE_PROP_BLOB, nullptr, 0, 0, 0, 0);
    addProp(kCrtcId, pCrtcDegamma, "DEGAMMA_LUT", DRM_MODE_PROP_BLOB, nullptr, 0, 0, 0, 0);
    addProp(kCrtcId, pCrtcCtm, "CTM", DRM_MODE_PROP_BLOB, nullptr, 0, 0, 0, 0);

    addProp(kPlaneId, pPlaneType, "type", DRM_MODE_PROP_ENUM | DRM_MODE_PROP_IMMUTABLE, kTypeEnums, 3, 0, 0, DRM_PLANE_TYPE_PRIMARY);
    addProp(kPlaneId, pPlaneFbId, "FB_ID", DRM_MODE_PROP_OBJECT, nullptr, 0, 0, 0, 0);
    addProp(kPlaneId, pPlaneCrtcId, "CRTC_ID", DRM_MODE_PROP_OBJECT, nullptr, 0, 0, 0, 0);
    addProp(kPlaneId, pPlaneInFenceFd, "IN_FENCE_FD", DRM_MODE_PROP_SIGNED_RANGE, nullptr, 0, (u64)-1, 0x7fffffff, (u64)-1);
    addProp(kPlaneId, pPlaneSrcX, "SRC_X", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kPlaneId, pPlaneSrcY, "SRC_Y", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kPlaneId, pPlaneSrcW, "SRC_W", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kPlaneId, pPlaneSrcH, "SRC_H", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kPlaneId, pPlaneCrtcX, "CRTC_X", DRM_MODE_PROP_SIGNED_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kPlaneId, pPlaneCrtcY, "CRTC_Y", DRM_MODE_PROP_SIGNED_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kPlaneId, pPlaneCrtcW, "CRTC_W", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kPlaneId, pPlaneCrtcH, "CRTC_H", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    // the value is the sentinel emuGetPropBlob serves the format blob for
    addProp(kPlaneId, pPlaneInFormats, "IN_FORMATS", DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE, nullptr, 0, 0, 0, pPlaneInFormats);

    addProp(kCursorPlaneId, pCursorType, "type", DRM_MODE_PROP_ENUM | DRM_MODE_PROP_IMMUTABLE, kTypeEnums, 3, 0, 0, DRM_PLANE_TYPE_CURSOR);
    addProp(kCursorPlaneId, pCursorFbId, "FB_ID", DRM_MODE_PROP_OBJECT, nullptr, 0, 0, 0, 0);
    addProp(kCursorPlaneId, pCursorCrtcId, "CRTC_ID", DRM_MODE_PROP_OBJECT, nullptr, 0, 0, 0, 0);
    addProp(kCursorPlaneId, pCursorSrcX, "SRC_X", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kCursorPlaneId, pCursorSrcY, "SRC_Y", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kCursorPlaneId, pCursorSrcW, "SRC_W", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kCursorPlaneId, pCursorSrcH, "SRC_H", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kCursorPlaneId, pCursorCrtcX, "CRTC_X", DRM_MODE_PROP_SIGNED_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kCursorPlaneId, pCursorCrtcY, "CRTC_Y", DRM_MODE_PROP_SIGNED_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kCursorPlaneId, pCursorCrtcW, "CRTC_W", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);
    addProp(kCursorPlaneId, pCursorCrtcH, "CRTC_H", DRM_MODE_PROP_RANGE, nullptr, 0, 0, ~0u, 0);

    addProp(kLeasePlaneId, pLeasePlaneType, "type", DRM_MODE_PROP_ENUM | DRM_MODE_PROP_IMMUTABLE, kTypeEnums, 3, 0, 0, DRM_PLANE_TYPE_PRIMARY);
}

int FakeKms::emuGetCap(drm_get_cap* c) {
    switch (c->capability) {
        case DRM_CAP_CURSOR_WIDTH:
        case DRM_CAP_CURSOR_HEIGHT:
            c->value = cursorCap;
            return 0;
        case DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP:
            c->value = noAsync ? 0 : 1;
            return 0;
        default:
            // syncobj and friends: the companion node answers truthfully
            return rawIoctl(renderFd, DRM_IOCTL_GET_CAP, c) == 0 ? 0 : -EINVAL;
    }
}

int FakeKms::emuGetResources(drm_mode_card_res* r) {
    static const u32 crtcs[] = {kCrtcId, kLeaseCrtcId};
    static const u32 conns[] = {kConnectorId, kLeaseConnectorId};
    static const u32 encs[] = {kEncoderId, kLeaseEncoderId};

    fillArray(r->crtc_id_ptr, r->count_crtcs, crtcs, 2);
    fillArray(r->connector_id_ptr, r->count_connectors, conns, 2);
    fillArray(r->encoder_id_ptr, r->count_encoders, encs, 2);
    r->count_fbs = 0;
    r->min_width = 640;
    r->max_width = 8192;
    r->min_height = 480;
    r->max_height = 8192;

    return 0;
}

// the connector's current mode list, at most kMaxModes; the tv and small
// sets model replugging displays that only do one size
u32 FakeKms::currentModes(drm_mode_modeinfo* modes) {
    if (modeSet == 1) {
        fillMode(modes[0], 1920, 1080, 60, true);

        return 1;
    }

    if (modeSet == 2) {
        fillMode(modes[0], 1280, 800, 60, true);

        return 1;
    }

    if (modeSet == 3) {
        fillMode(modes[0], 1366, 768, 60, true);

        return 1;
    }

    if (modeSet == 4) {
        return 0;
    }

    if (modeSet == 5) {
        // longer than a probe keeps: the tail past 32 is never looked at
        fillMode(modes[0], 1920, 1080, 60, false);
        fillMode(modes[1], 1280, 800, 50, false);
        fillMode(modes[2], 1280, 720, 60, false);

        for (u32 i = 3; i < kMaxModes; i++) {
            fillMode(modes[i], 640, 480, 60 + i, false);
        }

        return kMaxModes;
    }

    fillMode(modes[0], 1280, 800, 60, true);
    fillMode(modes[1], 1920, 1080, 60, false);

    return 2;
}

int FakeKms::emuGetConnector(drm_mode_get_connector* c) {
    // the lease connector unplugged between the offer and the request
    if (c->connector_id == kLeaseConnectorId && leaseFaultKind == 1) {
        return -ENOENT;
    }

    if (c->connector_id == kLeaseConnectorId) {
        drm_mode_modeinfo mode;

        fillMode(mode, 1920, 1080, 90, true);

        if (c->modes_ptr && c->count_modes >= 1) {
            memcpy((void*)(uintptr_t)c->modes_ptr, &mode, sizeof(mode));
        }

        c->count_modes = 1;

        Vector<u32> propIds;
        Vector<u64> propValues;

        for (const PropDef& p : props) {
            if (p.obj == kLeaseConnectorId) {
                propIds.pushBack(p.id);
                propValues.pushBack(p.value);
            }
        }

        fillArray(c->props_ptr, c->count_props, propIds.data(), (u32)propIds.length());

        if (c->prop_values_ptr && propValues.length()) {
            memcpy((void*)(uintptr_t)c->prop_values_ptr, propValues.data(), sizeof(u64) * propValues.length());
        }

        static const u32 leaseEncs[] = {kLeaseEncoderId};

        fillArray(c->encoders_ptr, c->count_encoders, leaseEncs, 1);
        c->encoder_id = kLeaseEncoderId;
        c->connector_type = DRM_MODE_CONNECTOR_DisplayPort;
        c->connector_type_id = 1;
        c->connection = 1;
        c->mm_width = 70;
        c->mm_height = 40;
        c->subpixel = 0;

        return 0;
    }

    if (c->connector_id != kConnectorId || connected == 2) {
        return -ENOENT;
    }

    drm_mode_modeinfo modes[kMaxModes];
    u32 nModes = connected ? currentModes(modes) : 0;

    if (c->modes_ptr && c->count_modes >= nModes) {
        memcpy((void*)(uintptr_t)c->modes_ptr, modes, sizeof(drm_mode_modeinfo) * nModes);
    }

    c->count_modes = nModes;

    Vector<u32> propIds;
    Vector<u64> propValues;

    for (const PropDef& p : props) {
        if (p.obj == kConnectorId) {
            propIds.pushBack(p.id);
            propValues.pushBack(p.value);
        }
    }

    fillArray(c->props_ptr, c->count_props, propIds.data(), (u32)propIds.length());

    if (c->prop_values_ptr && propValues.length()) {
        memcpy((void*)(uintptr_t)c->prop_values_ptr, propValues.data(), sizeof(u64) * propValues.length());
    }

    static const u32 encs[] = {kEncoderId};

    fillArray(c->encoders_ptr, c->count_encoders, encs, 1);
    c->encoder_id = unbound ? 0 : kEncoderId;
    // an internal panel takes its brightness from the backlight class, an
    // external one over ddc/ci; scenarios pick which
    c->connector_type = internalPanel ? DRM_MODE_CONNECTOR_eDP : DRM_MODE_CONNECTOR_HDMIA;
    c->connector_type_id = 1;
    c->connection = connected ? 1 : 2; // connected : disconnected
    c->mm_width = 340;
    c->mm_height = 210;
    c->subpixel = 0;

    return 0;
}

int FakeKms::emuGetEncoder(drm_mode_get_encoder* e) {
    if (e->encoder_id == kLeaseEncoderId && leaseFaultKind == 2) {
        return -ENOENT;
    }

    if (e->encoder_id == kLeaseEncoderId) {
        e->encoder_type = DRM_MODE_ENCODER_TMDS;
        e->crtc_id = kLeaseCrtcId;
        // the second crtc of the resource list, or only the desktop's
        e->possible_crtcs = leaseFaultKind == 3 ? 1 : 2;
        e->possible_clones = 0;

        return 0;
    }

    if (e->encoder_id != kEncoderId) {
        return -ENOENT;
    }

    e->encoder_type = DRM_MODE_ENCODER_TMDS;
    e->crtc_id = unbound ? 0 : kCrtcId;
    e->possible_crtcs = noCrtc ? 0 : 1;
    e->possible_clones = 0;

    return 0;
}

// A lease is a fresh handle on the same device. Nothing here polices what
// the lessee then does with it, and the returned fd is another reference to
// the companion node. The objects are not checked: the backend leases at
// least the offered connector (an empty request is a protocol error before
// it), with a crtc and planes from this device's own resource lists.
int FakeKms::emuCreateLease(drm_mode_create_lease* l) {
    if (leaseFaultKind == 4) {
        return -ENOSPC;
    }

    const u32* ids = (const u32*)(uintptr_t)l->object_ids;
    int fd = dup(renderFd);

    if (fd < 0) {
        return -errno;
    }

    // what went out, for scenarios to check the lessee got a whole pipe
    StringBuilder what;

    what << "fake-kms: lease"_sv;

    for (u32 i = 0; i < l->object_count; i++) {
        what << " "_sv << ids[i];
    }

    sysE << sv(what) << endL;
    l->fd = (u32)fd;
    l->lessee_id = ++lastLessee;

    return 0;
}

// the backend revokes only leases this device granted
int FakeKms::emuRevokeLease(drm_mode_revoke_lease*) {
    return 0;
}

int FakeKms::emuGetPlaneResources(drm_mode_get_plane_res* r) {
    static const u32 planes[] = {kLeasePlaneId, kPlaneId, kCursorPlaneId};

    fillArray(r->plane_id_ptr, r->count_planes, planes, 3);

    return 0;
}

// the backend asks only for planes the plane list above names
int FakeKms::emuGetPlane(drm_mode_get_plane* p) {
    fillArray(p->format_type_ptr, p->count_format_types, kFormats, (u32)(sizeof(kFormats) / sizeof(kFormats[0])));
    p->possible_crtcs = p->plane_id == kLeasePlaneId ? 2 : 1;
    p->crtc_id = 0;
    p->fb_id = 0;
    p->gamma_size = 0;

    return 0;
}

int FakeKms::emuObjGetProperties(drm_mode_obj_get_properties* o) {
    Vector<u32> ids;
    Vector<u64> values;

    for (const PropDef& p : props) {
        if (p.obj == o->obj_id) {
            ids.pushBack(p.id);
            values.pushBack(p.value);
        }
    }

    if (ids.empty()) {
        return -ENOENT;
    }

    fillArray(o->props_ptr, o->count_props, ids.data(), (u32)ids.length());

    if (o->prop_values_ptr && values.length()) {
        memcpy((void*)(uintptr_t)o->prop_values_ptr, values.data(), sizeof(u64) * values.length());
    }

    return 0;
}

int FakeKms::emuGetProperty(drm_mode_get_property* q) {
    PropDef* p = findProp(q->prop_id);

    if (!p) {
        return -ENOENT;
    }

    memset(q->name, 0, sizeof(q->name));

    size_t len = strlen(p->name);

    memcpy(q->name, p->name, len < sizeof(q->name) - 1 ? len : sizeof(q->name) - 1);
    q->flags = p->flags;

    if (p->flags & DRM_MODE_PROP_ENUM) {
        if (q->enum_blob_ptr && q->count_enum_blobs >= (u32)p->enumCount) {
            auto* out = (drm_mode_property_enum*)(uintptr_t)q->enum_blob_ptr;

            for (int i = 0; i < p->enumCount; i++) {
                memset(&out[i], 0, sizeof(out[i]));
                out[i].value = p->enums[i].value;

                size_t n = strlen(p->enums[i].name);

                memcpy(out[i].name, p->enums[i].name, n < sizeof(out[i].name) - 1 ? n : sizeof(out[i].name) - 1);
            }
        }

        q->count_enum_blobs = (u32)p->enumCount;
        q->count_values = 0;
    } else if (p->flags & (DRM_MODE_PROP_RANGE | DRM_MODE_PROP_SIGNED_RANGE)) {
        u64 range[2] = {p->rangeMin, p->rangeMax};

        fillArray(q->values_ptr, q->count_values, range, 2);
        q->count_enum_blobs = 0;
    } else {
        q->count_values = 0;
        q->count_enum_blobs = 0;
    }

    return 0;
}

int FakeKms::emuGetPropBlob(drm_mode_get_blob* b) {
    if (b->blob_id == pPlaneInFormats) {
        Vector<u8> data;

        buildInFormatsBlob(data, no10Bit, tiledOnly);

        if (b->data && b->length >= data.length()) {
            memcpy((void*)(uintptr_t)b->data, data.data(), data.length());
        }

        b->length = (u32)data.length();

        return 0;
    }

    FakeBlob* blob = findBlob(b->blob_id);

    if (!blob) {
        return -ENOENT;
    }

    if (b->data && b->length >= blob->data.length()) {
        memcpy((void*)(uintptr_t)b->data, blob->data.data(), blob->data.length());
    }

    b->length = (u32)blob->data.length();

    return 0;
}

int FakeKms::emuCreateBlob(drm_mode_create_blob* b) {
    auto* blob = new FakeBlob();

    blob->id = nextBlob++;
    blob->data.append((const u8*)(uintptr_t)b->data, b->length);
    blobs.pushBack(blob);
    b->blob_id = blob->id;

    return 0;
}

int FakeKms::emuDestroyBlob(drm_mode_destroy_blob* b) {
    for (size_t i = 0; i < blobs.length(); i++) {
        if (blobs[i]->id == b->blob_id) {
            delete blobs[i];
            blobs.mut(i) = blobs[blobs.length() - 1];
            blobs.popBack();

            return 0;
        }
    }

    return -ENOENT;
}

int FakeKms::emuPrimeFdToHandle(drm_prime_handle* p) {
    if (noPrime) {
        return -ENOTSUP;
    }

    if (failPrimeSkip > 0) {
        failPrimeSkip--;
    } else if (failPrimeCount > 0) {
        failPrimeCount--;

        return -failPrimeErr;
    }

    // one buffer object has one handle per drm fd, however many times and
    // through whichever of its dma-buf fds it is imported: two wl_buffers
    // on one BO come back with the same handle, as from the kernel
    struct stat st{};

    syscall(SYS_fstat, p->fd, &st);

    for (const FakeGem& gem : gems) {
        struct stat held{};

        syscall(SYS_fstat, gem.fd, &held);

        if (held.st_dev == st.st_dev && held.st_ino == st.st_ino) {
            p->handle = gem.handle;

            return 0;
        }
    }

    int dup = fcntl(p->fd, F_DUPFD_CLOEXEC, 0);

    if (dup < 0) {
        return -errno;
    }

    FakeGem gem;

    gem.handle = nextHandle++;
    gem.fd = dup;
    gem.refs = 1;
    gems.pushBack(gem);
    p->handle = gem.handle;

    return 0;
}

int FakeKms::emuGemClose(drm_gem_close* c) {
    for (size_t i = 0; i < gems.length(); i++) {
        if (gems[i].handle == c->handle) {
            close(gems[i].fd);
            gems.mut(i) = gems[gems.length() - 1];
            gems.popBack();

            return 0;
        }
    }

    return -EINVAL;
}

int FakeKms::emuCreateDumb(drm_mode_create_dumb* c) {
    if (!c->width || !c->height || c->bpp != 32) {
        return -EINVAL;
    }

    if (failDumbCount > 0) {
        failDumbCount--;

        return -ENOMEM;
    }

    FakeGem gem;

    // rows padded to 256 bytes, the way display engines want them: a
    // 1366-wide buffer is no longer width * 4 apart
    u32 pitch = (c->width * 4 + 255) & ~255u;

    gem.dumbSize = (u64)pitch * c->height;
    gem.fd = (int)syscall(SYS_memfd_create, "fake-kms-dumb", (unsigned)MFD_CLOEXEC);

    if (gem.fd < 0) {
        return -errno;
    }

    if (ftruncate(gem.fd, (off_t)gem.dumbSize) != 0) {
        int err = errno;

        close(gem.fd);

        return -err;
    }

    gem.handle = nextHandle++;
    gem.refs = 1;
    gems.pushBack(gem);
    c->handle = gem.handle;
    c->pitch = pitch;
    c->size = gem.dumbSize;

    return 0;
}

int FakeKms::emuMapDumb(drm_mode_map_dumb* m) {
    for (const FakeGem& gem : gems) {
        if (gem.handle == m->handle && gem.dumbSize) {
            m->offset = (u64)gem.handle << kDumbOffsetShift;

            return 0;
        }
    }

    return -EINVAL;
}

int FakeKms::emuDestroyDumb(drm_mode_destroy_dumb* d) {
    drm_gem_close c{};

    c.handle = d->handle;

    return emuGemClose(&c);
}

int FakeKms::emuAddFb2(drm_mode_fb_cmd2* f) {
    if (failAddFbSkip > 0) {
        failAddFbSkip--;
    } else if (failAddFbCount > 0) {
        failAddFbCount--;

        return -failAddFbErr;
    }

    for (size_t i = 0; i < gems.length(); i++) {
        if (gems[i].handle == f->handles[0]) {
            FakeFb fb;

            fb.id = nextFb++;
            fb.width = f->width;
            fb.height = f->height;
            fb.format = f->pixel_format;
            fb.modifier = f->modifier[0];
            fbs.pushBack(fb);
            f->fb_id = fb.id;

            return 0;
        }
    }

    return -EINVAL;
}

int FakeKms::emuRmFb(u32* id) {
    for (size_t i = 0; i < fbs.length(); i++) {
        if (fbs[i].id == *id) {
            fbs.mut(i) = fbs[fbs.length() - 1];
            fbs.popBack();

            return 0;
        }
    }

    return -ENOENT;
}

int FakeKms::emuAtomic(drm_mode_atomic* a) {
    if (a->flags & ~(u32)(DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_PAGE_FLIP_ASYNC)) {
        return -EINVAL;
    }

    bool test = a->flags & DRM_MODE_ATOMIC_TEST_ONLY;

    if (!test && flipPending) {
        return -EBUSY;
    }

    const u32* objs = (const u32*)(uintptr_t)a->objs_ptr;
    const u32* counts = (const u32*)(uintptr_t)a->count_props_ptr;
    const u32* propIds = (const u32*)(uintptr_t)a->props_ptr;
    const u64* values = (const u64*)(uintptr_t)a->prop_values_ptr;

    // validate first: unknown object/property is EINVAL either way
    u32 k = 0;

    for (u32 i = 0; i < a->count_objs; i++) {
        for (u32 j = 0; j < counts[i]; j++, k++) {
            PropDef* p = findProp(propIds[k]);

            if (!p || p->obj != objs[i]) {
                return -EINVAL;
            }

            // a connector that refuses the HDR color configuration
            // (wide-gamut colorspace or HDR metadata): the SDR fallback
            if (rejectColor && values[k] && (propIds[k] == pConnColorspace || propIds[k] == pConnHdrMeta)) {
                return -EINVAL;
            }

            // a plane that rejects framebuffers created after arming:
            // the compositor swapchain exists from boot, so this hits
            // exactly the direct-scanout import of a client fb
            if (failNewFbErr && !test && (propIds[k] == pPlaneFbId || propIds[k] == pCursorFbId) && (u32)values[k] >= failNewFbSince) {
                return -failNewFbErr;
            }

            // a display that cannot do hardware cursors: enabling the
            // cursor plane fails, shutting it off is fine — the shape
            // the compositor's cursor bisect is built for
            if (rejectCursorErr && propIds[k] == pCursorFbId && values[k]) {
                return -rejectCursorErr;
            }

            // a mode the connector does not currently offer is refused,
            // like a real display would after being swapped on hotplug
            if (propIds[k] == pCrtcModeId && values[k]) {
                FakeBlob* blob = findBlob((u32)values[k]);

                if (!blob) {
                    return -EINVAL;
                }

                const drm_mode_modeinfo* m = (const drm_mode_modeinfo*)blob->data.data();
                drm_mode_modeinfo offered[kMaxModes];
                u32 n = currentModes(offered);
                bool listed = false;

                for (u32 mi = 0; mi < n; mi++) {
                    listed = listed || (offered[mi].hdisplay == m->hdisplay && offered[mi].vdisplay == m->vdisplay && offered[mi].vrefresh == m->vrefresh);
                }

                if (!listed) {
                    return -EINVAL;
                }
            }
        }
    }

    if ((!test || failTestToo) && failCount > 0) {
        failCount--;

        return -failErr;
    }

    if (test) {
        return 0;
    }

    k = 0;

    for (u32 i = 0; i < a->count_objs; i++) {
        for (u32 j = 0; j < counts[i]; j++, k++) {
            PropDef* p = findProp(propIds[k]);

            // what the link was told to carry, for scenarios to hold the
            // compositor to: link depth, rgb range, the HDR metadata
            if (p->value != values[k] && (p->id == pConnMaxBpc || p->id == pConnBroadcastRgb)) {
                sysE << "fake-kms: "_sv << StringView(p->name) << " = "_sv << values[k] << endL;
            }

            // one-shot: scenarios wait for the cursor to reach its plane
            if (p->id == pCursorFbId && values[k] && !cursorOnLogged) {
                sysE << "fake-kms: cursor plane on"_sv << endL;
                cursorOnLogged = true;
            }

            if (p->value != values[k] && p->id == pConnHdrMeta && values[k]) {
                FakeBlob* blob = findBlob((u32)values[k]);

                if (blob && blob->data.length() >= sizeof(hdr_output_metadata)) {
                    const auto* meta = (const hdr_output_metadata*)blob->data.data();

                    sysE << "fake-kms: hdr metadata max_cll "_sv << (u64)meta->hdmi_metadata_type1.max_cll << endL;
                }
            }

            p->value = values[k];
        }
    }

    if ((a->flags & DRM_MODE_PAGE_FLIP_ASYNC) && !asyncFlipLogged) {
        // scenarios assert tearing engaged by this one-shot marker
        sysE << "fake-kms: async page flip"_sv << endL;
        asyncFlipLogged = true;
    }

    if (a->flags & DRM_MODE_PAGE_FLIP_EVENT) {
        flipPending = true;
        flipUserData = a->user_data;
        flipDueNs = nowNs() + 16666667ull;
        pthread_cond_signal(&cv);
    }

    return 0;
}

void FakeKms::flipLoop() {
    pthread_mutex_lock(&mu);

    for (;;) {
        if (!flipPending || flipsHeld) {
            pthread_cond_wait(&cv, &mu);

            continue;
        }

        u64 now = nowNs();

        if (now < flipDueNs) {
            timespec until{};

            until.tv_sec = (time_t)(flipDueNs / 1000000000ull);
            until.tv_nsec = (long)(flipDueNs % 1000000000ull);
            pthread_cond_timedwait(&cv, &mu, &until);

            continue;
        }

        struct {
            drm_event base;
            u64 user_data;
            u32 tv_sec, tv_usec, sequence, crtc_id;
        } ev{};

        ev.base.type = DRM_EVENT_FLIP_COMPLETE;
        ev.base.length = sizeof(ev);
        ev.user_data = flipUserData;
        ev.tv_sec = (u32)(now / 1000000000ull);
        ev.tv_usec = (u32)(now % 1000000000ull / 1000);
        ev.sequence = ++flipSeq;
        ev.crtc_id = kCrtcId;
        flipPending = false;
        flipsDone++;

        ssize_t n = write(eventFd, &ev, sizeof(ev));

        (void)n;
    }
}

// DDC/CI as a monitor speaks it: a Get VCP request is answered with the
// 11-byte reply, a Set VCP lands in the log for the scenario to read
void FakeKms::ddcLoop() {
    bool first = true;

    for (;;) {
        u8 msg[16];
        ssize_t n = read(ddcPeer, msg, sizeof(msg));

        if (n <= 0) {
            break;
        }

        // a monitor waking its DDC/CI engine drops the very first request
        if (first || ddcSilent) {
            first = false;

            continue;
        }

        if (n == 5 && msg[1] == 0x82 && msg[2] == 0x01) {
            u8 rep[11] = {0x6e, 0x88, 0x02, 0x00, msg[3], 0x00, (u8)(ddcMax >> 8), (u8)ddcMax, (u8)(ddcCur >> 8), (u8)ddcCur, 0};

            for (int i = 0; i < 10; i++) {
                rep[10] ^= rep[i];
            }

            ssize_t w = write(ddcPeer, rep, sizeof(rep));

            (void)w;
        } else if (n == 7 && msg[1] == 0x84 && msg[2] == 0x03) {
            ddcCur = (msg[4] << 8) | msg[5];
            // the emulator's log lines share one stream: the ioctl path
            // writes under the same lock
            pthread_mutex_lock(&mu);
            sysE << "fake-kms: ddc set vcp "_sv << (i64)msg[3] << " = "_sv << ddcCur << endL;
            pthread_mutex_unlock(&mu);
        }
    }

    close(ddcPeer);
}

bool FakeKms::isDdcBus(int fd) {
    struct stat st{};

    return ddcIno && syscall(SYS_fstat, fd, &st) == 0 && st.st_ino == ddcIno && st.st_dev == ddcDev;
}

int FakeKms::openDdc(StringView bus) {
    if (!ddcArmed) {
        return -ENOENT;
    }

    int ends[2];

    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, ends) != 0) {
        return -errno;
    }

    // i2c-dev reads never block: an unanswered request is a short read
    fcntl(ends[0], F_SETFL, O_NONBLOCK);

    struct stat st{};

    syscall(SYS_fstat, ends[0], &st);
    ddcIno = st.st_ino;
    ddcDev = st.st_dev;
    ddcPeer = ends[1];
    pthread_create(&ddcThread, nullptr, ddcThreadTrampoline, this);
    pthread_detach(ddcThread);
    pthread_mutex_lock(&mu);
    sysE << "fake-kms: ddc monitor on "_sv << bus << endL;
    pthread_mutex_unlock(&mu);

    return ends[0];
}

long FakeKms::fakeIoctl(unsigned long req, void* arg) {
    pthread_mutex_lock(&mu);

    long rc;

    // The kernel compares only the low 32 bits of an ioctl request. Callers
    // disagree about the rest: musl's _IOC yields a negative int for _IOWR
    // (dir bits reach the sign bit), so a libc-prototyped caller arrives
    // sign-extended while Mesa's raw-syscall path arrives zero-extended.
    // Truncate before dispatch so both spellings hit the same case.
    if (lookupFails((u32)req, arg)) {
        pthread_mutex_unlock(&mu);
        errno = EIO;

        return -1;
    }

    switch ((u32)req) {
        case DRM_IOCTL_SET_CLIENT_CAP:
            rc = 0;
            break;
        case DRM_IOCTL_GET_CAP:
            rc = emuGetCap((drm_get_cap*)arg);
            break;
        case DRM_IOCTL_MODE_GETRESOURCES:
            rc = emuGetResources((drm_mode_card_res*)arg);
            break;
        case DRM_IOCTL_MODE_GETCONNECTOR:
            rc = emuGetConnector((drm_mode_get_connector*)arg);
            break;
        case DRM_IOCTL_MODE_GETENCODER:
            rc = emuGetEncoder((drm_mode_get_encoder*)arg);
            break;
        case DRM_IOCTL_MODE_GETPLANERESOURCES:
            rc = emuGetPlaneResources((drm_mode_get_plane_res*)arg);
            break;
        case DRM_IOCTL_MODE_GETPLANE:
            rc = emuGetPlane((drm_mode_get_plane*)arg);
            break;
        case DRM_IOCTL_MODE_OBJ_GETPROPERTIES:
            rc = emuObjGetProperties((drm_mode_obj_get_properties*)arg);
            break;
        case DRM_IOCTL_MODE_GETPROPERTY:
            rc = emuGetProperty((drm_mode_get_property*)arg);
            break;
        case DRM_IOCTL_MODE_GETPROPBLOB:
            rc = emuGetPropBlob((drm_mode_get_blob*)arg);
            break;
        case DRM_IOCTL_MODE_CREATEPROPBLOB:
            rc = emuCreateBlob((drm_mode_create_blob*)arg);
            break;
        case DRM_IOCTL_MODE_DESTROYPROPBLOB:
            rc = emuDestroyBlob((drm_mode_destroy_blob*)arg);
            break;
        case DRM_IOCTL_PRIME_FD_TO_HANDLE:
            rc = emuPrimeFdToHandle((drm_prime_handle*)arg);
            break;
        case DRM_IOCTL_GEM_CLOSE:
            rc = emuGemClose((drm_gem_close*)arg);
            break;
        case DRM_IOCTL_MODE_ADDFB2:
            rc = emuAddFb2((drm_mode_fb_cmd2*)arg);
            break;
        case DRM_IOCTL_MODE_RMFB:
            rc = emuRmFb((u32*)arg);
            break;
        case DRM_IOCTL_MODE_ATOMIC:
            rc = emuAtomic((drm_mode_atomic*)arg);
            break;
        case DRM_IOCTL_MODE_CREATE_DUMB:
            rc = emuCreateDumb((drm_mode_create_dumb*)arg);
            break;
        case DRM_IOCTL_MODE_MAP_DUMB:
            rc = emuMapDumb((drm_mode_map_dumb*)arg);
            break;
        case DRM_IOCTL_MODE_DESTROY_DUMB:
            rc = emuDestroyDumb((drm_mode_destroy_dumb*)arg);
            break;
        case DRM_IOCTL_MODE_CREATE_LEASE:
            rc = emuCreateLease((drm_mode_create_lease*)arg);
            break;
        case DRM_IOCTL_MODE_REVOKE_LEASE:
            rc = emuRevokeLease((drm_mode_revoke_lease*)arg);
            break;
        default:
            if (_IOC_TYPE(req) == DRM_IOCTL_BASE && _IOC_NR(req) >= 0xBF && _IOC_NR(req) <= 0xCF && _IOC_NR(req) != 0xCE) {
                // the syncobj family (create..eventfd, minus GETFB2 at
                // 0xCE): real kernel objects on the companion render
                // node back explicit sync for real
                rc = rawIoctl(renderFd, req, arg);
                rc = rc == 0 ? 0 : -errno;
            } else {
                sysE << "fake-kms: unhandled drm ioctl nr "_sv << (i64)_IOC_NR(req) << " type "_sv << (i64)_IOC_TYPE(req) << " size "_sv << (i64)_IOC_SIZE(req) << " dir "_sv << (i64)_IOC_DIR(req) << endL;
                rc = -ENOTTY;
            }

            break;
    }

    pthread_mutex_unlock(&mu);

    if (rc < 0) {
        errno = (int)-rc;

        return -1;
    }

    return 0;
}

int FakeKms::dumbMemFd(unsigned long long off) {
    u32 handle = (u32)(off >> kDumbOffsetShift);
    int memFd = -1;

    pthread_mutex_lock(&mu);

    for (const FakeGem& gem : gems) {
        if (gem.handle == handle && gem.dumbSize) {
            memFd = gem.fd;
        }
    }

    pthread_mutex_unlock(&mu);

    return memFd;
}

// ---- libc interposition -----------------------------------------------
// The whole binary is static: defining these symbols here binds every
// caller (libdrm included) to them before musl's archive members are even
// considered. Non-fake fds forward through the raw syscall, bit for bit.

#ifdef __GLIBC__
using IoctlRequest = unsigned long;
#else
using IoctlRequest = int;
#endif

extern "C" int ioctl(int fd, IoctlRequest req, ...) {
    va_list ap;

    va_start(ap, req);

    void* arg = va_arg(ap, void*);

    va_end(ap);

    if (g && g->clientFd >= 0 && fd == g->clientFd) {
        return (int)g->fakeIoctl((unsigned long)req, arg);
    }

    // addressing the monitor on the emulated bus: an absent one NAKs
    if (g && (u32)req == I2C_SLAVE && g->isDdcBus(fd)) {
        if (g->ddcAbsent) {
            errno = ENXIO;

            return -1;
        }

        return 0;
    }

    long rc = syscall(SYS_ioctl, fd, (unsigned long)req, arg);

    return (int)rc;
}

extern "C" int fstat(int fd, struct stat* st) {
    // the fake fd claims the companion render node's identity so DeviceVk
    // pairs the real GPU with it and keeps the zero-copy scanout path
    long rc = syscall(SYS_fstat, g && g->clientFd >= 0 && fd == g->clientFd ? g->renderFd : fd, st);

    return (int)rc;
}

extern "C" void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off) {
    // a dumb-buffer mapping resolves through the handle tagged into the
    // MAP_DUMB offset token; everything else passes through untouched
    if (g && g->clientFd >= 0 && fd == g->clientFd) {
        int memFd = g->dumbMemFd((unsigned long long)off);

        if (memFd < 0) {
            errno = EINVAL;

            return MAP_FAILED;
        }

        fd = memFd;
        off = 0;
    }

    return (void*)syscall(SYS_mmap, addr, len, prot, flags, fd, off);
}

KmsIntercept* installInterceptor() {
    if (!g) {
        g = new FakeKms();
    }

    return g;
}

int FakeKms::openDevice() {
    int pipeFds[2];

    if (pipe2(pipeFds, O_CLOEXEC) != 0) {
        return -errno;
    }

    // the nodes behind the emulated card come from the scenario's staged
    // directory when it has one, like the backend's own node scan
    const char* staged = getenv("IMWAY_DRI_DIR");
    StringView dri = staged ? StringView(staged) : "/dev/dri"_sv;
    int render = -1;

    for (int i = 128; i < 136 && render < 0; i++) {
        auto& path = sb();

        path << dri << "/renderD"_sv << i;
        render = open(path.cStr(), O_RDWR | O_CLOEXEC);
    }

    // no render node (a virtual host): a card node still answers the fstat
    // identity and caps; every modeset ioctl stays emulated, and syncobj
    // forwards fail cleanly into "no explicit sync"
    for (int i = 0; i < 8 && render < 0; i++) {
        auto& path = sb();

        path << dri << "/card"_sv << i;
        render = open(path.cStr(), O_RDWR | O_CLOEXEC);
    }

    if (render < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);

        return -ENODEV;
    }

    eventFd = pipeFds[1];
    renderFd = render;
    internalPanel = getenv("IMWAY_FAKE_KMS_INTERNAL") != nullptr;
    rejectColor = getenv("IMWAY_FAKE_KMS_REJECT_COLOR") != nullptr;
    rejectCursorErr = getenv("IMWAY_FAKE_KMS_REJECT_CURSOR") ? EINVAL : 0;
    noPrime = getenv("IMWAY_FAKE_KMS_NO_PRIME") != nullptr;

    const char* drop = getenv("IMWAY_FAKE_KMS_DROP_PROPS");
    const char* zero = getenv("IMWAY_FAKE_KMS_ZERO_PROPS");
    const char* edid = getenv("IMWAY_FAKE_KMS_EDID");
    const char* bpc = getenv("IMWAY_FAKE_KMS_MAX_BPC");
    const char* minBpc = getenv("IMWAY_FAKE_KMS_MIN_BPC");
    const char* cursor = getenv("IMWAY_FAKE_KMS_CURSOR_CAP");
    const char* dumb = getenv("IMWAY_FAKE_KMS_FAIL_DUMB");
    const char* addFb = getenv("IMWAY_FAKE_KMS_FAIL_ADDFB");
    const char* ddc = getenv("IMWAY_FAKE_KMS_DDC");
    const char* commits = getenv("IMWAY_FAKE_KMS_FAIL_COMMITS");
    const char* lookups = getenv("IMWAY_FAKE_KMS_FAIL_LOOKUPS");

    noAsync = getenv("IMWAY_FAKE_KMS_NO_ASYNC") != nullptr;
    parseLookupFaults(lookups ? StringView(lookups) : StringView());

    dropProps = drop ? StringView(drop) : StringView();
    edidKind = !edid ? 0 : StringView(edid) == "sdr"_sv ? 1 : StringView(edid) == "no-bt2020"_sv ? 3 : 2;
    minBpcCap = minBpc ? StringView(minBpc).stou() : 6;
    maxBpcCap = bpc ? StringView(bpc).stou() : 16;
    legacyLimited = getenv("IMWAY_FAKE_KMS_LEGACY_RANGE") != nullptr;
    cursorCap = cursor ? StringView(cursor).stou() : 64;
    unbound = getenv("IMWAY_FAKE_KMS_UNBOUND") != nullptr;
    noCrtc = getenv("IMWAY_FAKE_KMS_NO_CRTC") != nullptr;
    no10Bit = getenv("IMWAY_FAKE_KMS_NO_10BIT") != nullptr;
    tiledOnly = getenv("IMWAY_FAKE_KMS_TILED_ONLY") != nullptr;
    zeroProps = zero ? StringView(zero) : StringView();
    failDumbCount = dumb ? (int)StringView(dumb).stou() : 0;
    // the N-th framebuffer from boot on fails: the cursor's comes first
    failAddFbErr = addFb ? ENOSPC : 0;
    failAddFbCount = addFb ? 1 : 0;
    failAddFbSkip = addFb ? (int)StringView(addFb).stou() - 1 : 0;
    ddcArmed = ddc && *ddc;
    ddcAbsent = ddc && StringView(ddc) == "absent"_sv;
    ddcSilent = ddc && StringView(ddc) == "silent"_sv;
    ddcMax = ddcArmed && !ddcAbsent && !ddcSilent ? (int)StringView(ddc).stou() : 0;
    ddcCur = ddcMax / 2;
    // a display that refuses to light up: the first N commits, tests too
    failErr = commits ? EINVAL : 0;
    failCount = commits ? (int)StringView(commits).stou() : 0;
    failTestToo = commits != nullptr;
    buildProps();
    pthread_create(&flipThread, nullptr, flipThreadTrampoline, this);
    // published last: the libc overrides start matching this fd only once
    // the emulator is fully assembled
    clientFd = pipeFds[0];

    return clientFd;
}

void FakeKms::setConnected(int value) {
    pthread_mutex_lock(&mu);
    connected = value;
    pthread_mutex_unlock(&mu);
}

void FakeKms::setModes(int set) {
    pthread_mutex_lock(&mu);
    modeSet = set;
    pthread_mutex_unlock(&mu);
}

void FakeKms::failCommits(int err, int count, bool testToo) {
    pthread_mutex_lock(&mu);
    failErr = err;
    failCount = count;
    failTestToo = testToo;
    pthread_mutex_unlock(&mu);
}

void FakeKms::failNewFb(int err) {
    pthread_mutex_lock(&mu);
    failNewFbErr = err;
    failNewFbSince = nextFb;
    pthread_mutex_unlock(&mu);
}

void FakeKms::failPrime(int err, int count, int skip) {
    pthread_mutex_lock(&mu);
    failPrimeErr = err;
    failPrimeCount = count;
    failPrimeSkip = skip;
    pthread_mutex_unlock(&mu);
}

void FakeKms::failAddFb(int err, int count) {
    pthread_mutex_lock(&mu);
    failAddFbErr = err;
    failAddFbCount = count;
    failAddFbSkip = 0;
    pthread_mutex_unlock(&mu);
}

void FakeKms::rejectCursor(int err) {
    pthread_mutex_lock(&mu);
    rejectCursorErr = err;
    pthread_mutex_unlock(&mu);
}

void FakeKms::parseLookupFaults(StringView rules) {
    lookupFaults.clear();

    while (!rules.empty()) {
        StringView rule, rest;

        if (rules.split(',', rule, rest)) {
            rules = rest;
        } else {
            rule = rules;
            rules = {};
        }

        StringView kind, target, skip, count;

        if (!rule.split(':', kind, rest)) {
            kind = rule;
            rest = {};
        }

        if (!rest.split(':', target, rest)) {
            target = rest;
            rest = {};
        }

        if (!rest.split(':', skip, count)) {
            skip = rest;
            count = {};
        }

        LookupFault f;

        static const struct {
            const char* kind;
            u32 req;
        } kinds[] = {
            {"props", DRM_IOCTL_MODE_OBJ_GETPROPERTIES},
            {"prop", DRM_IOCTL_MODE_GETPROPERTY},
            {"blob", DRM_IOCTL_MODE_GETPROPBLOB},
            {"plane", DRM_IOCTL_MODE_GETPLANE},
            {"createblob", DRM_IOCTL_MODE_CREATEPROPBLOB},
            {"resources", DRM_IOCTL_MODE_GETRESOURCES},
            {"planes", DRM_IOCTL_MODE_GETPLANERESOURCES},
            {"encoder", DRM_IOCTL_MODE_GETENCODER},
            {"connector", DRM_IOCTL_MODE_GETCONNECTOR},
            {"clientcap", DRM_IOCTL_SET_CLIENT_CAP},
            {"mapdumb", DRM_IOCTL_MODE_MAP_DUMB},
            {"cap", DRM_IOCTL_GET_CAP},
        };

        for (const auto& k : kinds) {
            if (kind == StringView(k.kind)) {
                f.req = k.req;
            }
        }

        f.id = (u32)target.stou();

        size_t n = target.length() < sizeof(f.name) - 1 ? target.length() : sizeof(f.name) - 1;

        // an empty target is a null view: nothing to copy, and memcpy takes
        // no null source even for zero bytes
        if (n) {
            memcpy(f.name, target.data(), n);
        }
        f.skip = skip.empty() ? 0 : (int)skip.stou();
        f.count = count.empty() || count == "-1"_sv ? -1 : (int)count.stou();
        lookupFaults.pushBack(f);
    }
}

bool FakeKms::lookupFails(u32 req, void* arg) {
    for (size_t i = 0; i < lookupFaults.length(); i++) {
        LookupFault& f = lookupFaults.mut(i);

        if (f.req != req || f.count == 0) {
            continue;
        }

        bool match = true;

        if (req == DRM_IOCTL_MODE_OBJ_GETPROPERTIES) {
            match = ((drm_mode_obj_get_properties*)arg)->obj_id == f.id;
        } else if (req == DRM_IOCTL_MODE_GETPLANE) {
            match = ((drm_mode_get_plane*)arg)->plane_id == f.id;
        } else if (req == DRM_IOCTL_MODE_GETENCODER) {
            match = ((drm_mode_get_encoder*)arg)->encoder_id == f.id;
        } else if (req == DRM_IOCTL_MODE_GETCONNECTOR) {
            match = ((drm_mode_get_connector*)arg)->connector_id == f.id;
        } else if (req == DRM_IOCTL_GET_CAP) {
            match = ((drm_get_cap*)arg)->capability == f.id;
        } else if (req == DRM_IOCTL_MODE_GETPROPERTY) {
            PropDef* p = findProp(((drm_mode_get_property*)arg)->prop_id);

            match = p && StringView(p->name) == StringView(f.name);
        } else if (req == DRM_IOCTL_MODE_GETPROPBLOB) {
            u32 blob = ((drm_mode_get_blob*)arg)->blob_id;

            match = false;

            for (const PropDef& p : props) {
                match = match || (StringView(p.name) == StringView(f.name) && p.value == blob);
            }
        }

        if (!match) {
            continue;
        }

        if (f.skip > 0) {
            f.skip--;

            continue;
        }

        if (f.count > 0) {
            f.count--;
        }

        return true;
    }

    return false;
}

void FakeKms::failLookups(StringView rules) {
    pthread_mutex_lock(&mu);
    parseLookupFaults(rules);
    pthread_mutex_unlock(&mu);
}

void FakeKms::leaseFault(int kind) {
    pthread_mutex_lock(&mu);
    leaseFaultKind = kind;
    pthread_mutex_unlock(&mu);
}

void FakeKms::holdFlips(bool hold) {
    pthread_mutex_lock(&mu);
    flipsHeld = hold;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

unsigned long long FakeKms::flips() {
    pthread_mutex_lock(&mu);

    unsigned long long n = flipsDone;

    pthread_mutex_unlock(&mu);

    return n;
}

int FakeKms::liveFbs() {
    pthread_mutex_lock(&mu);

    int n = (int)fbs.length();

    pthread_mutex_unlock(&mu);

    return n;
}

int FakeKms::liveGems() {
    pthread_mutex_lock(&mu);

    int n = (int)gems.length();

    pthread_mutex_unlock(&mu);

    return n;
}
