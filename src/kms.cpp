// DRM/KMS-бэкенд: atomic modeset + dumb-буферы, кадр рендерера копируется в scanout.

#include "kms.h"
#include "server.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/kd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    struct DumbBuffer {
        u32 handle = 0;
        u32 fbId = 0;
        u32 pitch = 0;
        u64 size = 0;
        u8* map = nullptr;
    };

    u32 getPropId(int fd, u32 objId, u32 objType, const char* name) {
        drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objId, objType);

        if (!props) {
            return 0;
        }

        u32 id = 0;

        for (u32 i = 0; i < props->count_props && !id; i++) {
            drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);

            if (p) {
                if (!strcmp(p->name, name)) {
                    id = p->prop_id;
                }

                drmModeFreeProperty(p);
            }
        }

        drmModeFreeObjectProperties(props);

        return id;
    }

    void pageFlipHandler(int, unsigned, unsigned, unsigned, void* data);
    void drmIoCb(struct ev_loop*, ev_io* w, int);

    struct KmsImpl: public Kms {
        Server* server = nullptr;
        int fd = -1;
        int ttyFd = -1;
        long oldKbMode = -1;

        u32 connectorId = 0;
        u32 crtcId = 0;
        u32 planeId = 0;
        drmModeModeInfo mode{};
        u32 modeBlob = 0;

        // property ids
        u32 connCrtcId = 0;
        u32 crtcModeId = 0, crtcActive = 0;
        u32 plFbId = 0, plCrtcId = 0;
        u32 plSrcX = 0, plSrcY = 0, plSrcW = 0, plSrcH = 0;
        u32 plCrtcX = 0, plCrtcY = 0, plCrtcW = 0, plCrtcH = 0;

        DumbBuffer bufs[2];
        int nextBuf = 0;
        bool flipPending = false;
        bool modeSet = false;

        ev_io drmIo{};

        KmsImpl(Server& srv, const char* path);
        ~KmsImpl() noexcept override;

        void pickOutput();
        void createDumb(DumbBuffer& b);
        bool commit(DumbBuffer& b, bool doModeset);
        void setupVt();
        void restoreVt() noexcept;

        bool start() override;
        void present(const void* pixels) override;
    };

    void pageFlipHandler(int, unsigned, unsigned, unsigned, void* data) {
        ((KmsImpl*)data)->flipPending = false;
    }

    void drmIoCb(struct ev_loop*, ev_io* w, int) {
        drmEventContext ctx{};

        ctx.version = 2;
        ctx.page_flip_handler = pageFlipHandler;
        drmHandleEvent(((KmsImpl*)w->data)->fd, &ctx);
    }
}

KmsImpl::KmsImpl(Server& srv, const char* path)
    : server(&srv)
{
    fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);

    if (fd < 0) {
        Errno().raise(StringBuilder() << "kms: open "_sv << path);
    }

    // atomic обязателен
    STD_VERIFY(drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) == 0);
    STD_VERIFY(drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0);

    pickOutput();

    connCrtcId = getPropId(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    crtcModeId = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    crtcActive = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    plFbId = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "FB_ID");
    plCrtcId = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    plSrcX = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_X");
    plSrcY = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    plSrcW = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_W");
    plSrcH = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_H");
    plCrtcX = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    plCrtcY = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    plCrtcW = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    plCrtcH = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_H");

    drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &modeBlob);

    for (auto& b : bufs) {
        createDumb(b);
    }

    srv.outW = mode.hdisplay;
    srv.outH = mode.vdisplay;
    srv.hz = mode.vrefresh > 0 ? mode.vrefresh : 60.0;

    sysO << "imway: kms "_sv << path << ": "_sv << mode.hdisplay << "x"_sv << mode.vdisplay
         << "@"_sv << mode.vrefresh << ", connector "_sv << connectorId << ", crtc "_sv << crtcId
         << ", plane "_sv << planeId << endL;
}

KmsImpl::~KmsImpl() noexcept {
    if (fd < 0) {
        return;
    }

    ev_io_stop(server->loop, &drmIo);
    restoreVt();

    for (auto& b : bufs) {
        if (b.map && b.map != MAP_FAILED) {
            munmap(b.map, b.size);
        }

        if (b.fbId) {
            drmModeRmFB(fd, b.fbId);
        }

        if (b.handle) {
            drm_mode_destroy_dumb d{};

            d.handle = b.handle;
            drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
        }
    }

    if (modeBlob) {
        drmModeDestroyPropertyBlob(fd, modeBlob);
    }

    close(fd);
    fd = -1;
}

void KmsImpl::pickOutput() {
    drmModeRes* res = drmModeGetResources(fd);

    STD_VERIFY(res); // drmModeGetResources сломался

    drmModeConnector* conn = nullptr;

    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector* c = drmModeGetConnector(fd, res->connectors[i]);

        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
        } else if (c) {
            drmModeFreeConnector(c);
        }
    }

    STD_VERIFY(conn); // нет подключённых коннекторов

    connectorId = conn->connector_id;
    mode = conn->modes[0]; // первый = preferred

    for (int i = 0; i < conn->count_modes; i++) {
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode = conn->modes[i];

            break;
        }
    }

    // CRTC через encoder->possible_crtcs
    drmModeEncoder* enc = conn->encoder_id ? drmModeGetEncoder(fd, conn->encoder_id) : nullptr;

    if (!enc && conn->count_encoders > 0) {
        enc = drmModeGetEncoder(fd, conn->encoders[0]);
    }

    STD_VERIFY(enc); // нет энкодера

    if (enc->crtc_id) {
        crtcId = enc->crtc_id;
    } else {
        for (int i = 0; i < res->count_crtcs; i++) {
            if (enc->possible_crtcs & (1 << i)) {
                crtcId = res->crtcs[i];

                break;
            }
        }
    }

    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    STD_VERIFY(crtcId); // нет CRTC

    // primary plane этого CRTC
    int crtcIndex = -1;

    for (int i = 0; i < res->count_crtcs; i++) {
        if (res->crtcs[i] == crtcId) {
            crtcIndex = i;
        }
    }

    drmModeFreeResources(res);

    drmModePlaneRes* planes = drmModeGetPlaneResources(fd);

    for (u32 i = 0; i < planes->count_planes && !planeId; i++) {
        drmModePlane* p = drmModeGetPlane(fd, planes->planes[i]);

        if (!p) {
            continue;
        }

        if (p->possible_crtcs & (1u << crtcIndex)) {
            u32 typeProp = getPropId(fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type");
            drmModeObjectProperties* pp =
                drmModeObjectGetProperties(fd, p->plane_id, DRM_MODE_OBJECT_PLANE);

            for (u32 j = 0; j < pp->count_props; j++) {
                if (pp->props[j] == typeProp && pp->prop_values[j] == DRM_PLANE_TYPE_PRIMARY) {
                    planeId = p->plane_id;
                }
            }

            drmModeFreeObjectProperties(pp);
        }

        drmModeFreePlane(p);
    }

    drmModeFreePlaneResources(planes);
    STD_VERIFY(planeId); // нет primary plane
}

void KmsImpl::createDumb(DumbBuffer& b) {
    drm_mode_create_dumb create{};

    create.width = mode.hdisplay;
    create.height = mode.vdisplay;
    create.bpp = 32;
    STD_VERIFY(drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) == 0);

    b.handle = create.handle;
    b.pitch = create.pitch;
    b.size = create.size;

    u32 handles[4] = {b.handle}, pitches[4] = {b.pitch}, offsets[4] = {};

    STD_VERIFY(drmModeAddFB2(fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888, handles,
                             pitches, offsets, &b.fbId, 0) == 0);

    drm_mode_map_dumb mapReq{};

    mapReq.handle = b.handle;
    STD_VERIFY(drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mapReq) == 0);

    b.map = (u8*)mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                      (off_t)mapReq.offset);
    STD_VERIFY(b.map != MAP_FAILED);
}

bool KmsImpl::commit(DumbBuffer& b, bool doModeset) {
    drmModeAtomicReq* req = drmModeAtomicAlloc();

    if (doModeset) {
        drmModeAtomicAddProperty(req, connectorId, connCrtcId, crtcId);
        drmModeAtomicAddProperty(req, crtcId, crtcModeId, modeBlob);
        drmModeAtomicAddProperty(req, crtcId, crtcActive, 1);
    }

    drmModeAtomicAddProperty(req, planeId, plFbId, b.fbId);
    drmModeAtomicAddProperty(req, planeId, plCrtcId, crtcId);
    drmModeAtomicAddProperty(req, planeId, plSrcX, 0);
    drmModeAtomicAddProperty(req, planeId, plSrcY, 0);
    drmModeAtomicAddProperty(req, planeId, plSrcW, (u64)mode.hdisplay << 16);
    drmModeAtomicAddProperty(req, planeId, plSrcH, (u64)mode.vdisplay << 16);
    drmModeAtomicAddProperty(req, planeId, plCrtcX, 0);
    drmModeAtomicAddProperty(req, planeId, plCrtcY, 0);
    drmModeAtomicAddProperty(req, planeId, plCrtcW, mode.hdisplay);
    drmModeAtomicAddProperty(req, planeId, plCrtcH, mode.vdisplay);

    u32 flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

    if (doModeset) {
        flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
    }

    int ret = drmModeAtomicCommit(fd, req, flags, this);

    drmModeAtomicFree(req);

    if (ret != 0) {
        if (errno != EBUSY) {
            sysE << "imway: kms atomic commit failed, errno "_sv << errno << endL;
        }

        return false;
    }

    flipPending = true;

    return true;
}

void KmsImpl::setupVt() {
    // выключить обработку клавиатуры ядром и текстовый режим консоли,
    // иначе ввод дублируется в getty, а курсор консоли мигает поверх
    ttyFd = open("/dev/tty1", O_RDWR | O_CLOEXEC);

    if (ttyFd < 0) {
        sysE << "imway: /dev/tty1 unavailable, input will leak to console"_sv << endL;

        return;
    }

    ioctl(ttyFd, KDGKBMODE, &oldKbMode);
    ioctl(ttyFd, KDSKBMODE, K_OFF);
    ioctl(ttyFd, KDSETMODE, KD_GRAPHICS);
}

void KmsImpl::restoreVt() noexcept {
    if (ttyFd < 0) {
        return;
    }

    ioctl(ttyFd, KDSETMODE, KD_TEXT);

    if (oldKbMode != -1) {
        ioctl(ttyFd, KDSKBMODE, oldKbMode);
    }

    close(ttyFd);
    ttyFd = -1;
}

bool KmsImpl::start() {
    setupVt();
    ev_io_init(&drmIo, drmIoCb, fd, EV_READ);
    drmIo.data = this;
    ev_io_start(server->loop, &drmIo);

    // первый кадр: чёрный, с модесетом
    memset(bufs[0].map, 0, bufs[0].size);

    if (!commit(bufs[0], true)) {
        sysE << "imway: kms modeset failed"_sv << endL;

        return false;
    }

    nextBuf = 1;
    modeSet = true;

    return true;
}

void KmsImpl::present(const void* pixels) {
    if (!modeSet || flipPending) { // предыдущий flip ещё в полёте — дропаем кадр
        return;
    }

    DumbBuffer& b = bufs[nextBuf];
    const auto* src = (const u8*)pixels;
    size_t row = (size_t)mode.hdisplay * 4;

    for (int y = 0; y < mode.vdisplay; y++) {
        memcpy(b.map + (size_t)y * b.pitch, src + (size_t)y * row, row);
    }

    if (commit(b, false)) {
        nextBuf ^= 1;
    }
}

Kms::~Kms() noexcept {
}

Kms* Kms::create(ObjPool* pool, Server& server, const char* devPath) {
    return pool->make<KmsImpl>(server, devPath);
}
