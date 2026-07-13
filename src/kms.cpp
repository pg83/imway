#include "kms.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/kd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "server.hpp"

namespace {

struct DumbBuffer {
    uint32_t handle = 0;
    uint32_t fb_id = 0;
    uint32_t pitch = 0;
    uint64_t size = 0;
    uint8_t* map = nullptr;
};

uint32_t get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type, const char* name) {
    drmModeObjectProperties* props = drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (!props) return 0;
    uint32_t id = 0;
    for (uint32_t i = 0; i < props->count_props && !id; i++) {
        drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);
        if (p) {
            if (!strcmp(p->name, name)) id = p->prop_id;
            drmModeFreeProperty(p);
        }
    }
    drmModeFreeObjectProperties(props);
    return id;
}

} // namespace

struct Kms {
    Server* server = nullptr;
    int fd = -1;
    int tty_fd = -1;
    long old_kb_mode = -1;

    uint32_t connector_id = 0;
    uint32_t crtc_id = 0;
    uint32_t plane_id = 0;
    drmModeModeInfo mode{};
    uint32_t mode_blob = 0;

    // property ids
    uint32_t conn_crtc_id = 0;
    uint32_t crtc_mode_id = 0, crtc_active = 0;
    uint32_t pl_fb_id = 0, pl_crtc_id = 0;
    uint32_t pl_src_x = 0, pl_src_y = 0, pl_src_w = 0, pl_src_h = 0;
    uint32_t pl_crtc_x = 0, pl_crtc_y = 0, pl_crtc_w = 0, pl_crtc_h = 0;

    DumbBuffer bufs[2];
    int next_buf = 0;
    bool flip_pending = false;
    bool mode_set = false;

    ev_io drm_io{};

    bool init(Server&, const char* path);
    bool pick_output();
    bool create_dumb(DumbBuffer&);
    bool start();
    void present(const void* pixels);
    bool commit(DumbBuffer&, bool modeset);
    void setup_vt();
    void restore_vt();
    void finish();
};

namespace {

void page_flip_handler(int, unsigned, unsigned, unsigned, void* data) {
    ((Kms*)data)->flip_pending = false;
}

void drm_io_cb(struct ev_loop*, ev_io* w, int) {
    drmEventContext ctx{};
    ctx.version = 2;
    ctx.page_flip_handler = page_flip_handler;
    drmHandleEvent(((Kms*)w->data)->fd, &ctx);
}

} // namespace

bool Kms::pick_output() {
    drmModeRes* res = drmModeGetResources(fd);
    if (!res) {
        std::fprintf(stderr, "kms: drmModeGetResources: %s\n", strerror(errno));
        return false;
    }
    drmModeConnector* conn = nullptr;
    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector* c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
            conn = c;
        else if (c)
            drmModeFreeConnector(c);
    }
    if (!conn) {
        std::fprintf(stderr, "kms: нет подключённых коннекторов\n");
        drmModeFreeResources(res);
        return false;
    }
    connector_id = conn->connector_id;
    mode = conn->modes[0]; // первый = preferred
    for (int i = 0; i < conn->count_modes; i++)
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode = conn->modes[i];
            break;
        }

    // CRTC через encoder->possible_crtcs
    drmModeEncoder* enc =
        conn->encoder_id ? drmModeGetEncoder(fd, conn->encoder_id) : nullptr;
    if (!enc && conn->count_encoders > 0) enc = drmModeGetEncoder(fd, conn->encoders[0]);
    if (!enc) {
        std::fprintf(stderr, "kms: нет энкодера\n");
        return false;
    }
    if (enc->crtc_id) {
        crtc_id = enc->crtc_id;
    } else {
        for (int i = 0; i < res->count_crtcs; i++)
            if (enc->possible_crtcs & (1 << i)) {
                crtc_id = res->crtcs[i];
                break;
            }
    }
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    if (!crtc_id) {
        std::fprintf(stderr, "kms: нет CRTC\n");
        return false;
    }

    // primary plane этого CRTC
    int crtc_index = -1;
    drmModeRes* res2 = drmModeGetResources(fd);
    for (int i = 0; i < res2->count_crtcs; i++)
        if (res2->crtcs[i] == crtc_id) crtc_index = i;
    drmModeFreeResources(res2);

    drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
    for (uint32_t i = 0; i < planes->count_planes && !plane_id; i++) {
        drmModePlane* p = drmModeGetPlane(fd, planes->planes[i]);
        if (!p) continue;
        if (p->possible_crtcs & (1u << crtc_index)) {
            uint32_t type_prop = get_prop_id(fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type");
            drmModeObjectProperties* pp =
                drmModeObjectGetProperties(fd, p->plane_id, DRM_MODE_OBJECT_PLANE);
            for (uint32_t j = 0; j < pp->count_props; j++)
                if (pp->props[j] == type_prop && pp->prop_values[j] == DRM_PLANE_TYPE_PRIMARY)
                    plane_id = p->plane_id;
            drmModeFreeObjectProperties(pp);
        }
        drmModeFreePlane(p);
    }
    drmModeFreePlaneResources(planes);
    if (!plane_id) {
        std::fprintf(stderr, "kms: нет primary plane\n");
        return false;
    }
    return true;
}

bool Kms::create_dumb(DumbBuffer& b) {
    drm_mode_create_dumb create{};
    create.width = mode.hdisplay;
    create.height = mode.vdisplay;
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        std::fprintf(stderr, "kms: CREATE_DUMB: %s\n", strerror(errno));
        return false;
    }
    b.handle = create.handle;
    b.pitch = create.pitch;
    b.size = create.size;

    uint32_t handles[4] = {b.handle}, pitches[4] = {b.pitch}, offsets[4] = {};
    if (drmModeAddFB2(fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888, handles, pitches,
                      offsets, &b.fb_id, 0) != 0) {
        std::fprintf(stderr, "kms: AddFB2: %s\n", strerror(errno));
        return false;
    }
    drm_mode_map_dumb map_req{};
    map_req.handle = b.handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) != 0) return false;
    b.map = (uint8_t*)mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                           (off_t)map_req.offset);
    return b.map != MAP_FAILED;
}

bool Kms::commit(DumbBuffer& b, bool modeset) {
    drmModeAtomicReq* req = drmModeAtomicAlloc();
    if (modeset) {
        drmModeAtomicAddProperty(req, connector_id, conn_crtc_id, crtc_id);
        drmModeAtomicAddProperty(req, crtc_id, crtc_mode_id, mode_blob);
        drmModeAtomicAddProperty(req, crtc_id, crtc_active, 1);
    }
    drmModeAtomicAddProperty(req, plane_id, pl_fb_id, b.fb_id);
    drmModeAtomicAddProperty(req, plane_id, pl_crtc_id, crtc_id);
    drmModeAtomicAddProperty(req, plane_id, pl_src_x, 0);
    drmModeAtomicAddProperty(req, plane_id, pl_src_y, 0);
    drmModeAtomicAddProperty(req, plane_id, pl_src_w, (uint64_t)mode.hdisplay << 16);
    drmModeAtomicAddProperty(req, plane_id, pl_src_h, (uint64_t)mode.vdisplay << 16);
    drmModeAtomicAddProperty(req, plane_id, pl_crtc_x, 0);
    drmModeAtomicAddProperty(req, plane_id, pl_crtc_y, 0);
    drmModeAtomicAddProperty(req, plane_id, pl_crtc_w, mode.hdisplay);
    drmModeAtomicAddProperty(req, plane_id, pl_crtc_h, mode.vdisplay);

    uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
    if (modeset) flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
    int ret = drmModeAtomicCommit(fd, req, flags, this);
    drmModeAtomicFree(req);
    if (ret != 0) {
        if (errno != EBUSY) std::fprintf(stderr, "kms: atomic commit: %s\n", strerror(errno));
        return false;
    }
    flip_pending = true;
    return true;
}

void Kms::setup_vt() {
    // выключить обработку клавиатуры ядром и текстовый режим консоли,
    // иначе ввод дублируется в getty, а курсор консоли мигает поверх
    tty_fd = open("/dev/tty1", O_RDWR | O_CLOEXEC);
    if (tty_fd < 0) {
        std::fprintf(stderr, "kms: /dev/tty1 недоступен (%s) — ввод продублируется в консоль\n",
                     strerror(errno));
        return;
    }
    ioctl(tty_fd, KDGKBMODE, &old_kb_mode);
    ioctl(tty_fd, KDSKBMODE, K_OFF);
    ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
}

void Kms::restore_vt() {
    if (tty_fd < 0) return;
    ioctl(tty_fd, KDSETMODE, KD_TEXT);
    if (old_kb_mode != -1) ioctl(tty_fd, KDSKBMODE, old_kb_mode);
    close(tty_fd);
    tty_fd = -1;
}

bool Kms::init(Server& srv, const char* path) {
    server = &srv;
    fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "kms: open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0 ||
        drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        std::fprintf(stderr, "kms: atomic не поддержан\n");
        return false;
    }
    if (!pick_output()) return false;

    conn_crtc_id = get_prop_id(fd, connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    crtc_mode_id = get_prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    crtc_active = get_prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    pl_fb_id = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
    pl_crtc_id = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    pl_src_x = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
    pl_src_y = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    pl_src_w = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
    pl_src_h = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
    pl_crtc_x = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    pl_crtc_y = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    pl_crtc_w = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    pl_crtc_h = get_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");

    drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &mode_blob);

    for (auto& b : bufs)
        if (!create_dumb(b)) return false;

    srv.out_w = mode.hdisplay;
    srv.out_h = mode.vdisplay;
    srv.hz = mode.vrefresh > 0 ? mode.vrefresh : 60.0;

    std::printf("imway: kms %s: %dx%d@%d, connector %u, crtc %u, plane %u\n", path,
                mode.hdisplay, mode.vdisplay, mode.vrefresh, connector_id, crtc_id, plane_id);
    return true;
}

bool Kms::start() {
    setup_vt();
    ev_io_init(&drm_io, drm_io_cb, fd, EV_READ);
    drm_io.data = this;
    ev_io_start(server->loop, &drm_io);

    // первый кадр: чёрный, с модесетом
    memset(bufs[0].map, 0, bufs[0].size);
    if (!commit(bufs[0], true)) {
        std::fprintf(stderr, "kms: модесет не прошёл\n");
        return false;
    }
    next_buf = 1;
    mode_set = true;
    return true;
}

void Kms::present(const void* pixels) {
    if (!mode_set || flip_pending) return; // предыдущий flip ещё в полёте — дропаем кадр
    DumbBuffer& b = bufs[next_buf];
    const auto* src = (const uint8_t*)pixels;
    size_t row = (size_t)mode.hdisplay * 4;
    for (int y = 0; y < mode.vdisplay; y++)
        memcpy(b.map + (size_t)y * b.pitch, src + (size_t)y * row, row);
    if (commit(b, false)) next_buf ^= 1;
}

void Kms::finish() {
    if (fd < 0) return;
    ev_io_stop(server->loop, &drm_io);
    restore_vt();
    for (auto& b : bufs) {
        if (b.map && b.map != MAP_FAILED) munmap(b.map, b.size);
        if (b.fb_id) drmModeRmFB(fd, b.fb_id);
        if (b.handle) {
            drm_mode_destroy_dumb d{};
            d.handle = b.handle;
            drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
        }
    }
    if (mode_blob) drmModeDestroyPropertyBlob(fd, mode_blob);
    close(fd);
    fd = -1;
}

Kms* kms_create(Server& server, const char* dev_path) {
    auto* k = new Kms();
    if (!k->init(server, dev_path)) {
        k->finish();
        delete k;
        return nullptr;
    }
    return k;
}

bool kms_start(Kms* k) { return k && k->start(); }

void kms_present(Kms* k, const void* pixels) {
    if (k) k->present(pixels);
}

void kms_destroy(Kms* k) {
    if (!k) return;
    k->finish();
    delete k;
}
