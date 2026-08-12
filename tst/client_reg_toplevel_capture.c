// ext-foreign-toplevel-list + toplevel image-capture-source: the handle
// found by app_id must open a copy-capture session constrained to that
// window's size, and the captured pixels are the window's own — not the
// output around it.

#include "wl_util.h"

#include <ext-foreign-toplevel-list-v1-client-protocol.h>
#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>

static struct ext_foreign_toplevel_list_v1* list;
static struct ext_foreign_toplevel_image_capture_source_manager_v1* tl_source_mgr;
static struct ext_image_copy_capture_manager_v1* copy_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, ext_foreign_toplevel_list_v1_interface.name))
        list = wl_registry_bind(r, name, &ext_foreign_toplevel_list_v1_interface, 1);
    else if (!strcmp(iface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name))
        tl_source_mgr = wl_registry_bind(r, name,
            &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1);
    else if (!strcmp(iface, ext_image_copy_capture_manager_v1_interface.name))
        copy_mgr = wl_registry_bind(r, name, &ext_image_copy_capture_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

// ---- foreign-toplevel-list handles ----
static struct ext_foreign_toplevel_handle_v1* target_handle;
static struct ext_foreign_toplevel_handle_v1* pending;
static char pending_app[128];
static int target_closed;

static void h_closed(void* d, struct ext_foreign_toplevel_handle_v1* h) {
    (void)d;
    if (h == target_handle)
        target_closed = 1;
}
static void h_done(void* d, struct ext_foreign_toplevel_handle_v1* h) {
    (void)d;
    if (!strcmp(pending_app, "tl-cap-target") && !target_handle && h == pending)
        target_handle = h;
    if (h == pending) {
        pending = NULL;
        pending_app[0] = 0;
    }
}
static void h_title(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* t) {
    (void)d; (void)h; (void)t;
}
static void h_app_id(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* a) {
    (void)d;
    if (h == pending)
        snprintf(pending_app, sizeof(pending_app), "%s", a);
}
static void h_identifier(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* i) {
    (void)d; (void)h;
    if (!i[0]) {
        fprintf(stderr, "empty identifier\n");
        exit(1);
    }
}
static const struct ext_foreign_toplevel_handle_v1_listener handle_listener = {
    .closed = h_closed,
    .done = h_done,
    .title = h_title,
    .app_id = h_app_id,
    .identifier = h_identifier,
};

static void l_toplevel(void* d, struct ext_foreign_toplevel_list_v1* l,
                       struct ext_foreign_toplevel_handle_v1* h) {
    (void)d; (void)l;
    pending = h;
    ext_foreign_toplevel_handle_v1_add_listener(h, &handle_listener, NULL);
}
static void l_finished(void* d, struct ext_foreign_toplevel_list_v1* l) { (void)d; (void)l; }
static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
    .toplevel = l_toplevel,
    .finished = l_finished,
};

// ---- copy-capture session ----
static uint32_t cap_w, cap_h;
static int have_xrgb, constraints_done, session_stopped;

static void on_buffer_size(void* d, struct ext_image_copy_capture_session_v1* s,
                           uint32_t w, uint32_t h) {
    (void)d; (void)s;
    cap_w = w; cap_h = h;
}
static void on_shm_format(void* d, struct ext_image_copy_capture_session_v1* s, uint32_t f) {
    (void)d; (void)s;
    if (f == WL_SHM_FORMAT_XRGB8888) have_xrgb = 1;
}
static void on_dmabuf_device(void* d, struct ext_image_copy_capture_session_v1* s, struct wl_array* a) {
    (void)d;(void)s;(void)a;
}
static void on_dmabuf_format(void* d, struct ext_image_copy_capture_session_v1* s, uint32_t f, struct wl_array* m) {
    (void)d;(void)s;(void)f;(void)m;
}
static void on_done(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    constraints_done = 1;
}
static void on_stopped(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    session_stopped = 1;
}
static const struct ext_image_copy_capture_session_v1_listener session_listener = {
    .buffer_size = on_buffer_size,
    .shm_format = on_shm_format,
    .dmabuf_device = on_dmabuf_device,
    .dmabuf_format = on_dmabuf_format,
    .done = on_done,
    .stopped = on_stopped,
};

static int frame_ready, frame_failed;

static void on_transform(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t t) { (void)d;(void)f;(void)t; }
static void on_damage(void* d, struct ext_image_copy_capture_frame_v1* f, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d;(void)f;(void)x;(void)y;(void)w;(void)h;
}
static void on_ptime(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)d;(void)f;(void)hi;(void)lo;(void)ns;
}
static void on_ready(void* d, struct ext_image_copy_capture_frame_v1* f) { (void)d;(void)f; frame_ready = 1; }
static void on_failed(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t r) {
    (void)d;(void)f;
    fprintf(stderr, "capture failed: %u\n", r);
    frame_failed = 1;
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = on_transform,
    .damage = on_damage,
    .presentation_time = on_ptime,
    .ready = on_ready,
    .failed = on_failed,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!list || !tl_source_mgr || !copy_mgr) {
        fprintf(stderr, "missing globals (list=%p src=%p copy=%p)\n",
                (void*)list, (void*)tl_source_mgr, (void*)copy_mgr);
        return 2;
    }

    // listen before mapping: the announcements ride the map commits
    ext_foreign_toplevel_list_v1_add_listener(list, &list_listener, NULL);

    // decoy below, target mapped last (topmost): the capture must be sized
    // and filled by the target alone
    struct wl_toplevel_ctx decoy, target;
    wl_make_toplevel(&decoy, "tl-cap-decoy", 420, 320, 0xFF00FF00u);
    wl_make_toplevel(&target, "tl-cap-target", 260, 180, 0xFFFF00FFu);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    if (!target_handle) {
        fprintf(stderr, "target toplevel never appeared in the list\n");
        return 1;
    }

    struct ext_image_capture_source_v1* source =
        ext_foreign_toplevel_image_capture_source_manager_v1_create_source(tl_source_mgr, target_handle);
    struct ext_image_copy_capture_session_v1* session =
        ext_image_copy_capture_manager_v1_create_session(copy_mgr, source, 0);
    ext_image_copy_capture_session_v1_add_listener(session, &session_listener, NULL);

    while (!constraints_done && !session_stopped && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (session_stopped) return 1;
    if (cap_w != 260 || cap_h != 180 || !have_xrgb) {
        fprintf(stderr, "constraints are not the window size: %ux%u xrgb=%d\n",
                cap_w, cap_h, have_xrgb);
        return 1;
    }

    int stride = (int)cap_w * 4, size = stride * (int)cap_h;
    int fd = memfd_create("tl-capture-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return 2;
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, (int)cap_w, (int)cap_h,
                                                         stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    struct ext_image_copy_capture_frame_v1* frame =
        ext_image_copy_capture_session_v1_create_frame(session);
    ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, NULL);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, (int)cap_w, (int)cap_h);
    ext_image_copy_capture_frame_v1_capture(frame);

    while (!frame_ready && !frame_failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (frame_failed) return 1;

    long magenta = 0, total = (long)cap_w * cap_h;
    for (long i = 0; i < total; i++) {
        if ((px[i] & 0xffffffu) == 0xff00ffu)
            magenta++;
    }
    if (magenta < total * 8 / 10) {
        fprintf(stderr, "captured window is not the target: %ld/%ld magenta\n", magenta, total);
        return 1;
    }

    printf("toplevel capture done\n");
    return 0;
}
