#include "fake_kms.h"

#include "kms_intercept.h"
#include "log.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/str/view.h>
#include <std/sys/types.h>

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

    // property ids, one flat namespace across objects
    enum : u32 {
        pConnCrtcId = 201,
        pConnColorspace,
        pConnMaxBpc,
        pConnBroadcastRgb,
        pConnHdrMeta,
        pConnEdid,
        pConnLinkBpc,
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

    struct FakeKms {
        int clientFd = -1; // handed to the compositor; events are read here
        int eventFd = -1;  // emulator's write end
        int renderFd = -1; // companion real node: syncobjs, identity
        dev_t renderDev = 0;

        pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
        pthread_t flipThread{};

        bool connected = true;
        int modeSet = 0; // 0 default, 1 tv (1080p only), 2 small (800p only)
        bool noPrime = false;
        bool asyncFlipLogged = false;
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
        int failNewFbErr = 0;
        u32 failNewFbSince = 0;
        int failPrimeErr = 0;
        int failPrimeCount = 0;
        int failPrimeSkip = 0;
        int failAddFbErr = 0;
        int failAddFbCount = 0;
        int rejectCursorErr = 0;
        bool rejectColor = false;

        u64 flips = 0;
    };

    FakeKms* g = nullptr;

    u64 nowNs() {
        timespec ts{};

        clock_gettime(CLOCK_MONOTONIC, &ts);

        return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
    }

    long rawIoctl(int fd, unsigned long req, void* arg) {
        long rc = syscall(SYS_ioctl, fd, req, arg);

        return rc;
    }

    PropDef* findProp(u32 id) {
        for (size_t i = 0; i < g->props.length(); i++) {
            if (g->props[i].id == id) {
                return &g->props.mut(i);
            }
        }

        return nullptr;
    }

    FakeBlob* findBlob(u32 id) {
        for (FakeBlob* b : g->blobs) {
            if (b->id == id) {
                return b;
            }
        }

        return nullptr;
    }

    void addProp(u32 obj, u32 id, const char* name, u32 flags, const PropEnum* enums, int enumCount, u64 mn, u64 mx, u64 value) {
        PropDef p;

        p.id = id;
        p.obj = obj;
        p.name = name;
        p.flags = flags;
        p.enums = enums;
        p.enumCount = enumCount;
        p.rangeMin = mn;
        p.rangeMax = mx;
        p.value = value;
        g->props.pushBack(p);
    }

    // A minimal but structurally valid EDID 1.4 with one CTA-861 extension:
    // sRGB chromaticity, a 1280x800 preferred timing, BT.2020 RGB
    // colorimetry and PQ HDR static metadata (~1000 nit peak, 400 nit
    // maxFALL, 0.1 nit floor). libdisplay-info reads it like a real HDR
    // panel's, which opens the compositor's positive HDR path.
    u32 makeEdidBlob() {
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

        for (int block = 0; block < 2; block++) {
            u8 sum = 0;

            for (int i = 0; i < 127; i++) {
                sum = (u8)(sum + e[block * 128 + i]);
            }

            e[block * 128 + 127] = (u8)(0u - sum);
        }

        auto* blob = new FakeBlob();

        blob->id = g->nextBlob++;
        blob->data.append(e, 256);
        g->blobs.pushBack(blob);

        return blob->id;
    }

    void buildProps() {
        addProp(kConnectorId, pConnCrtcId, "CRTC_ID", DRM_MODE_PROP_OBJECT, nullptr, 0, 0, 0, 0);
        addProp(kConnectorId, pConnColorspace, "Colorspace", DRM_MODE_PROP_ENUM, kColorspaceEnums, 3, 0, 0, 0);
        addProp(kConnectorId, pConnMaxBpc, "max bpc", DRM_MODE_PROP_RANGE, nullptr, 0, 6, 16, 10);
        addProp(kConnectorId, pConnBroadcastRgb, "Broadcast RGB", DRM_MODE_PROP_ENUM, kBroadcastEnums, 3, 0, 0, 0);
        addProp(kConnectorId, pConnHdrMeta, "HDR_OUTPUT_METADATA", DRM_MODE_PROP_BLOB, nullptr, 0, 0, 0, 0);
        addProp(kConnectorId, pConnEdid, "EDID", DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE, nullptr, 0, 0, 0, makeEdidBlob());

        // what the link actually negotiated; a scenario boots it low to
        // exercise the HDR degradation ladder
        const char* linkBpc = getenv("IMWAY_FAKE_KMS_LINK_BPC");

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
        addProp(kPlaneId, pPlaneInFormats, "IN_FORMATS", DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE, nullptr, 0, 0, 0, 0);

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

    int emuVersion(drm_version* v) {
        static const char kName[] = "fakekms";
        static const char kDate[] = "2026";
        static const char kDesc[] = "imway userspace kms emulator";

        if (v->name && v->name_len >= sizeof(kName) - 1) {
            memcpy(v->name, kName, sizeof(kName) - 1);
        }

        if (v->date && v->date_len >= sizeof(kDate) - 1) {
            memcpy(v->date, kDate, sizeof(kDate) - 1);
        }

        if (v->desc && v->desc_len >= sizeof(kDesc) - 1) {
            memcpy(v->desc, kDesc, sizeof(kDesc) - 1);
        }

        v->version_major = 1;
        v->version_minor = 0;
        v->version_patchlevel = 0;
        v->name_len = sizeof(kName) - 1;
        v->date_len = sizeof(kDate) - 1;
        v->desc_len = sizeof(kDesc) - 1;

        return 0;
    }

    int emuGetCap(drm_get_cap* c) {
        switch (c->capability) {
            case DRM_CAP_CURSOR_WIDTH:
            case DRM_CAP_CURSOR_HEIGHT:
                c->value = 64;
                return 0;
            case DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP:
                c->value = 1;
                return 0;
            case DRM_CAP_ADDFB2_MODIFIERS:
            case DRM_CAP_PRIME:
            case DRM_CAP_TIMESTAMP_MONOTONIC:
                c->value = c->capability == DRM_CAP_PRIME ? 3 : 1;
                return 0;
            default:
                // syncobj and friends: the companion node answers truthfully
                return rawIoctl(g->renderFd, DRM_IOCTL_GET_CAP, c) == 0 ? 0 : -EINVAL;
        }
    }

    int emuGetResources(drm_mode_card_res* r) {
        static const u32 crtcs[] = {kCrtcId};
        static const u32 conns[] = {kConnectorId};
        static const u32 encs[] = {kEncoderId};

        fillArray(r->crtc_id_ptr, r->count_crtcs, crtcs, 1);
        fillArray(r->connector_id_ptr, r->count_connectors, conns, 1);
        fillArray(r->encoder_id_ptr, r->count_encoders, encs, 1);
        r->count_fbs = 0;
        r->min_width = 640;
        r->max_width = 8192;
        r->min_height = 480;
        r->max_height = 8192;

        return 0;
    }

    // the connector's current mode list; the tv and small sets model
    // replugging displays that only do one size
    u32 currentModes(drm_mode_modeinfo* modes) {
        if (g->modeSet == 1) {
            fillMode(modes[0], 1920, 1080, 60, true);

            return 1;
        }

        if (g->modeSet == 2) {
            fillMode(modes[0], 1280, 800, 60, true);

            return 1;
        }

        fillMode(modes[0], 1280, 800, 60, true);
        fillMode(modes[1], 1920, 1080, 60, false);

        return 2;
    }

    int emuGetConnector(drm_mode_get_connector* c) {
        if (c->connector_id != kConnectorId) {
            return -ENOENT;
        }

        drm_mode_modeinfo modes[2];
        u32 nModes = g->connected ? currentModes(modes) : 0;

        if (c->modes_ptr && c->count_modes >= nModes) {
            memcpy((void*)(uintptr_t)c->modes_ptr, modes, sizeof(drm_mode_modeinfo) * nModes);
        }

        c->count_modes = nModes;

        Vector<u32> propIds;
        Vector<u64> propValues;

        for (const PropDef& p : g->props) {
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
        c->encoder_id = kEncoderId;
        c->connector_type = DRM_MODE_CONNECTOR_HDMIA;
        c->connector_type_id = 1;
        c->connection = g->connected ? 1 : 2; // connected : disconnected
        c->mm_width = 340;
        c->mm_height = 210;
        c->subpixel = 0;

        return 0;
    }

    int emuGetEncoder(drm_mode_get_encoder* e) {
        if (e->encoder_id != kEncoderId) {
            return -ENOENT;
        }

        e->encoder_type = DRM_MODE_ENCODER_TMDS;
        e->crtc_id = kCrtcId;
        e->possible_crtcs = 1;
        e->possible_clones = 0;

        return 0;
    }

    int emuGetPlaneResources(drm_mode_get_plane_res* r) {
        static const u32 planes[] = {kPlaneId, kCursorPlaneId};

        fillArray(r->plane_id_ptr, r->count_planes, planes, 2);

        return 0;
    }

    int emuGetPlane(drm_mode_get_plane* p) {
        if (p->plane_id != kPlaneId && p->plane_id != kCursorPlaneId) {
            return -ENOENT;
        }

        fillArray(p->format_type_ptr, p->count_format_types, kFormats, (u32)(sizeof(kFormats) / sizeof(kFormats[0])));
        p->possible_crtcs = 1;
        p->crtc_id = 0;
        p->fb_id = 0;
        p->gamma_size = 0;

        return 0;
    }

    int emuObjGetProperties(drm_mode_obj_get_properties* o) {
        Vector<u32> ids;
        Vector<u64> values;

        for (const PropDef& p : g->props) {
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

    int emuGetProperty(drm_mode_get_property* q) {
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

    // IN_FORMATS: the header, the format list, then one modifier struct
    // (LINEAR) whose mask covers every format
    void buildInFormatsBlob(Vector<u8>& out) {
        constexpr u32 nFmt = (u32)(sizeof(kFormats) / sizeof(kFormats[0]));
        struct drm_format_modifier_blob hdr{};

        hdr.version = 1;
        hdr.count_formats = nFmt;
        hdr.formats_offset = sizeof(hdr);
        hdr.count_modifiers = 1;
        hdr.modifiers_offset = sizeof(hdr) + sizeof(kFormats);

        struct drm_format_modifier mod{};

        mod.formats = (1ull << nFmt) - 1;
        mod.offset = 0;
        mod.modifier = DRM_FORMAT_MOD_LINEAR;

        out.append((const u8*)&hdr, sizeof(hdr));
        out.append((const u8*)kFormats, sizeof(kFormats));
        out.append((const u8*)&mod, sizeof(mod));
    }

    int emuGetPropBlob(drm_mode_get_blob* b) {
        if (b->blob_id == pPlaneInFormats) {
            Vector<u8> data;

            buildInFormatsBlob(data);

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

    int emuCreateBlob(drm_mode_create_blob* b) {
        auto* blob = new FakeBlob();

        blob->id = g->nextBlob++;
        blob->data.append((const u8*)(uintptr_t)b->data, b->length);
        g->blobs.pushBack(blob);
        b->blob_id = blob->id;

        return 0;
    }

    int emuDestroyBlob(drm_mode_destroy_blob* b) {
        for (size_t i = 0; i < g->blobs.length(); i++) {
            if (g->blobs[i]->id == b->blob_id) {
                delete g->blobs[i];
                g->blobs.mut(i) = g->blobs[g->blobs.length() - 1];
                g->blobs.popBack();

                return 0;
            }
        }

        return -ENOENT;
    }

    int emuPrimeFdToHandle(drm_prime_handle* p) {
        if (g->noPrime) {
            return -ENOTSUP;
        }

        if (g->failPrimeSkip > 0) {
            g->failPrimeSkip--;
        } else if (g->failPrimeCount > 0) {
            g->failPrimeCount--;

            return -g->failPrimeErr;
        }

        int dup = fcntl(p->fd, F_DUPFD_CLOEXEC, 0);

        if (dup < 0) {
            return -errno;
        }

        FakeGem gem;

        gem.handle = g->nextHandle++;
        gem.fd = dup;
        gem.refs = 1;
        g->gems.pushBack(gem);
        p->handle = gem.handle;

        return 0;
    }

    int emuGemClose(drm_gem_close* c) {
        for (size_t i = 0; i < g->gems.length(); i++) {
            if (g->gems[i].handle == c->handle) {
                close(g->gems[i].fd);
                g->gems.mut(i) = g->gems[g->gems.length() - 1];
                g->gems.popBack();

                return 0;
            }
        }

        return -EINVAL;
    }

    // Dumb buffers are memfds: the compositor's cursor plane (and the
    // no-zero-copy fallback path) mmaps them through the interposer below.
    // The map offset is a token, handle-tagged; the real mapping always
    // starts at the memfd's origin.
    constexpr int kDumbOffsetShift = 20;

    int emuCreateDumb(drm_mode_create_dumb* c) {
        if (!c->width || !c->height || c->bpp != 32) {
            return -EINVAL;
        }

        FakeGem gem;

        gem.dumbSize = (u64)c->width * 4 * c->height;
        gem.fd = (int)syscall(SYS_memfd_create, "fake-kms-dumb", (unsigned)MFD_CLOEXEC);

        if (gem.fd < 0) {
            return -errno;
        }

        if (ftruncate(gem.fd, (off_t)gem.dumbSize) != 0) {
            int err = errno;

            close(gem.fd);

            return -err;
        }

        gem.handle = g->nextHandle++;
        gem.refs = 1;
        g->gems.pushBack(gem);
        c->handle = gem.handle;
        c->pitch = c->width * 4;
        c->size = gem.dumbSize;

        return 0;
    }

    int emuMapDumb(drm_mode_map_dumb* m) {
        for (const FakeGem& gem : g->gems) {
            if (gem.handle == m->handle && gem.dumbSize) {
                m->offset = (u64)gem.handle << kDumbOffsetShift;

                return 0;
            }
        }

        return -EINVAL;
    }

    int emuDestroyDumb(drm_mode_destroy_dumb* d) {
        drm_gem_close c{};

        c.handle = d->handle;

        return emuGemClose(&c);
    }

    int emuAddFb2(drm_mode_fb_cmd2* f) {
        if (g->failAddFbCount > 0) {
            g->failAddFbCount--;

            return -g->failAddFbErr;
        }

        for (size_t i = 0; i < g->gems.length(); i++) {
            if (g->gems[i].handle == f->handles[0]) {
                FakeFb fb;

                fb.id = g->nextFb++;
                fb.width = f->width;
                fb.height = f->height;
                fb.format = f->pixel_format;
                fb.modifier = f->modifier[0];
                g->fbs.pushBack(fb);
                f->fb_id = fb.id;

                return 0;
            }
        }

        return -EINVAL;
    }

    int emuRmFb(u32* id) {
        for (size_t i = 0; i < g->fbs.length(); i++) {
            if (g->fbs[i].id == *id) {
                g->fbs.mut(i) = g->fbs[g->fbs.length() - 1];
                g->fbs.popBack();

                return 0;
            }
        }

        return -ENOENT;
    }

    int emuAtomic(drm_mode_atomic* a) {
        if (a->flags & ~(u32)(DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_PAGE_FLIP_ASYNC)) {
            return -EINVAL;
        }

        bool test = a->flags & DRM_MODE_ATOMIC_TEST_ONLY;

        if (!test && g->flipPending) {
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
                if (g->rejectColor && values[k] && (propIds[k] == pConnColorspace || propIds[k] == pConnHdrMeta)) {
                    return -EINVAL;
                }

                // a plane that rejects framebuffers created after arming:
                // the compositor swapchain exists from boot, so this hits
                // exactly the direct-scanout import of a client fb
                if (g->failNewFbErr && !test && (propIds[k] == pPlaneFbId || propIds[k] == pCursorFbId) && (u32)values[k] >= g->failNewFbSince) {
                    return -g->failNewFbErr;
                }

                // a display that cannot do hardware cursors: enabling the
                // cursor plane fails, shutting it off is fine — the shape
                // the compositor's cursor bisect is built for
                if (g->rejectCursorErr && propIds[k] == pCursorFbId && values[k]) {
                    return -g->rejectCursorErr;
                }

                // a mode the connector does not currently offer is refused,
                // like a real display would after being swapped on hotplug
                if (propIds[k] == pCrtcModeId && values[k]) {
                    FakeBlob* blob = findBlob((u32)values[k]);

                    if (!blob) {
                        return -EINVAL;
                    }

                    const drm_mode_modeinfo* m = (const drm_mode_modeinfo*)blob->data.data();
                    drm_mode_modeinfo offered[2];
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

        if (!test && g->failCount > 0) {
            g->failCount--;

            return -g->failErr;
        }

        if (test) {
            return 0;
        }

        k = 0;

        for (u32 i = 0; i < a->count_objs; i++) {
            for (u32 j = 0; j < counts[i]; j++, k++) {
                PropDef* p = findProp(propIds[k]);

                p->value = values[k];
            }
        }

        if ((a->flags & DRM_MODE_PAGE_FLIP_ASYNC) && !g->asyncFlipLogged) {
            // scenarios assert tearing engaged by this one-shot marker
            sysE << "fake-kms: async page flip"_sv << endL;
            g->asyncFlipLogged = true;
        }

        if (a->flags & DRM_MODE_PAGE_FLIP_EVENT) {
            g->flipPending = true;
            g->flipUserData = a->user_data;
            g->flipDueNs = nowNs() + 16666667ull;
            pthread_cond_signal(&g->cv);
        }

        return 0;
    }

    void* flipThreadMain(void*) {
        pthread_mutex_lock(&g->mu);

        for (;;) {
            if (!g->flipPending) {
                pthread_cond_wait(&g->cv, &g->mu);

                continue;
            }

            u64 now = nowNs();

            if (now < g->flipDueNs) {
                timespec until{};

                until.tv_sec = (time_t)(g->flipDueNs / 1000000000ull);
                until.tv_nsec = (long)(g->flipDueNs % 1000000000ull);
                pthread_cond_timedwait(&g->cv, &g->mu, &until);

                continue;
            }

            struct {
                drm_event base;
                u64 user_data;
                u32 tv_sec, tv_usec, sequence, crtc_id;
            } ev{};

            ev.base.type = DRM_EVENT_FLIP_COMPLETE;
            ev.base.length = sizeof(ev);
            ev.user_data = g->flipUserData;
            ev.tv_sec = (u32)(now / 1000000000ull);
            ev.tv_usec = (u32)(now % 1000000000ull / 1000);
            ev.sequence = ++g->flipSeq;
            ev.crtc_id = kCrtcId;
            g->flipPending = false;
            g->flips++;

            ssize_t n = write(g->eventFd, &ev, sizeof(ev));

            (void)n;
        }

        return nullptr;
    }

    long fakeIoctl(unsigned long req, void* arg) {
        pthread_mutex_lock(&g->mu);

        long rc;

        switch (req) {
            case DRM_IOCTL_VERSION:
                rc = emuVersion((drm_version*)arg);
                break;
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
                rc = -ENOTSUP;
                break;
            default:
                if (_IOC_TYPE(req) == DRM_IOCTL_BASE && _IOC_NR(req) >= 0xBF && _IOC_NR(req) <= 0xCF && _IOC_NR(req) != 0xCE) {
                    // the syncobj family (create..eventfd, minus GETFB2 at
                    // 0xCE): real kernel objects on the companion render
                    // node back explicit sync for real
                    rc = rawIoctl(g->renderFd, req, arg);
                    rc = rc == 0 ? 0 : -errno;
                } else {
                    sysE << "fake-kms: unhandled drm ioctl nr "_sv << (i64)_IOC_NR(req) << endL;
                    rc = -ENOTTY;
                }

                break;
        }

        pthread_mutex_unlock(&g->mu);

        if (rc < 0) {
            errno = (int)-rc;

            return -1;
        }

        return 0;
    }
}

// ---- libc interposition -----------------------------------------------
// The whole binary is static: defining these symbols here binds every
// caller (libdrm included) to them before musl's archive members are even
// considered. Non-fake fds forward through the raw syscall, bit for bit.

extern "C" int ioctl(int fd, int req, ...) {
    va_list ap;

    va_start(ap, req);

    void* arg = va_arg(ap, void*);

    va_end(ap);

    if (g && fd == g->clientFd) {
        return (int)fakeIoctl((unsigned long)(unsigned int)req, arg);
    }

    long rc = syscall(SYS_ioctl, fd, (unsigned long)(unsigned int)req, arg);

    return (int)rc;
}

extern "C" int fstat(int fd, struct stat* st) {
    // the fake fd claims the companion render node's identity so DeviceVk
    // pairs the real GPU with it and keeps the zero-copy scanout path
    long rc = syscall(SYS_fstat, g && fd == g->clientFd ? g->renderFd : fd, st);

    return (int)rc;
}

extern "C" void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off) {
    // a dumb-buffer mapping resolves through the handle tagged into the
    // MAP_DUMB offset token; everything else passes through untouched
    if (g && fd == g->clientFd) {
        u32 handle = (u32)((u64)off >> kDumbOffsetShift);
        int memFd = -1;

        pthread_mutex_lock(&g->mu);

        for (const FakeGem& gem : g->gems) {
            if (gem.handle == handle && gem.dumbSize) {
                memFd = gem.fd;
            }
        }

        pthread_mutex_unlock(&g->mu);

        if (memFd < 0) {
            errno = EINVAL;

            return MAP_FAILED;
        }

        fd = memFd;
        off = 0;
    }

    return (void*)syscall(SYS_mmap, addr, len, prot, flags, fd, off);
}

namespace {
    // the emulator's scripting face; state lives in g, created lazily by
    // openDevice so the libc overrides stay transparent until then
    struct FakeKmsIntercept: KmsIntercept {
        int openDevice() override;
        void setConnected(bool connected) override;
        void setModes(int set) override;
        void failCommits(int err, int count) override;
        void failNewFb(int err) override;
        void failPrime(int err, int count, int skip) override;
        void failAddFb(int err, int count) override;
        void rejectCursor(int err) override;
        unsigned long long flips() override;
    };

    FakeKmsIntercept interceptor;
}

KmsIntercept* installInterceptor() {
    return &interceptor;
}

int FakeKmsIntercept::openDevice() {
    if (g) {
        return -EBUSY;
    }

    int pipeFds[2];

    if (pipe2(pipeFds, O_CLOEXEC) != 0) {
        return -errno;
    }

    int render = -1;

    for (int i = 128; i < 136 && render < 0; i++) {
        char path[32] = "/dev/dri/renderD1";
        int n = (int)strlen(path);

        path[n] = (char)('0' + (i / 10) % 10);
        path[n + 1] = (char)('0' + i % 10);
        path[n + 2] = 0;
        render = open(path, O_RDWR | O_CLOEXEC);
    }

    if (render < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);

        return -ENODEV;
    }

    g = new FakeKms();
    g->clientFd = pipeFds[0];
    g->eventFd = pipeFds[1];
    g->renderFd = render;
    g->rejectColor = getenv("IMWAY_FAKE_KMS_REJECT_COLOR") != nullptr;
    g->rejectCursorErr = getenv("IMWAY_FAKE_KMS_REJECT_CURSOR") ? EINVAL : 0;
    g->noPrime = getenv("IMWAY_FAKE_KMS_NO_PRIME") != nullptr;
    buildProps();
    pthread_create(&g->flipThread, nullptr, flipThreadMain, nullptr);

    return g->clientFd;
}

void FakeKmsIntercept::setConnected(bool connected) {
    pthread_mutex_lock(&g->mu);
    g->connected = connected;
    pthread_mutex_unlock(&g->mu);
}

void FakeKmsIntercept::setModes(int set) {
    pthread_mutex_lock(&g->mu);
    g->modeSet = set;
    pthread_mutex_unlock(&g->mu);
}

void FakeKmsIntercept::failCommits(int err, int count) {
    pthread_mutex_lock(&g->mu);
    g->failErr = err;
    g->failCount = count;
    pthread_mutex_unlock(&g->mu);
}

void FakeKmsIntercept::failNewFb(int err) {
    pthread_mutex_lock(&g->mu);
    g->failNewFbErr = err;
    g->failNewFbSince = g->nextFb;
    pthread_mutex_unlock(&g->mu);
}

void FakeKmsIntercept::failPrime(int err, int count, int skip) {
    pthread_mutex_lock(&g->mu);
    g->failPrimeErr = err;
    g->failPrimeCount = count;
    g->failPrimeSkip = skip;
    pthread_mutex_unlock(&g->mu);
}

void FakeKmsIntercept::failAddFb(int err, int count) {
    pthread_mutex_lock(&g->mu);
    g->failAddFbErr = err;
    g->failAddFbCount = count;
    pthread_mutex_unlock(&g->mu);
}

void FakeKmsIntercept::rejectCursor(int err) {
    pthread_mutex_lock(&g->mu);
    g->rejectCursorErr = err;
    pthread_mutex_unlock(&g->mu);
}

unsigned long long FakeKmsIntercept::flips() {
    pthread_mutex_lock(&g->mu);

    unsigned long long n = g->flips;

    pthread_mutex_unlock(&g->mu);

    return n;
}
