// Toplevel capture when the window does not hold still. A session sized for
// a window that has since grown must bounce the frame with fresh
// constraints and then deliver at the new size; a window that unmapped
// stops the session and fails its frames as stopped, and a new session on
// it stops at once; a handle whose window is gone yields a source whose
// sessions stop — never one that captures the whole output instead; and a
// window wider than the output cannot be delivered at all.

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
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

// ---- foreign-toplevel-list handles, found by app_id ----
#define MAX_HANDLES 8

static struct {
    struct ext_foreign_toplevel_handle_v1* handle;
    char app_id[64];
    int done, closed;
} handles[MAX_HANDLES];
static int handle_count;

static int handle_slot(struct ext_foreign_toplevel_handle_v1* h) {
    for (int i = 0; i < handle_count; i++) {
        if (handles[i].handle == h)
            return i;
    }
    return -1;
}

static void h_closed(void* d, struct ext_foreign_toplevel_handle_v1* h) {
    (void)d;
    int i = handle_slot(h);
    if (i >= 0)
        handles[i].closed = 1;
}
static void h_done(void* d, struct ext_foreign_toplevel_handle_v1* h) {
    (void)d;
    int i = handle_slot(h);
    if (i >= 0)
        handles[i].done = 1;
}
static void h_title(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* t) {
    (void)d; (void)h; (void)t;
}
static void h_app_id(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* a) {
    (void)d;
    int i = handle_slot(h);
    if (i >= 0)
        snprintf(handles[i].app_id, sizeof(handles[i].app_id), "%s", a);
}
static void h_identifier(void* d, struct ext_foreign_toplevel_handle_v1* h, const char* id) {
    (void)d; (void)h; (void)id;
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
    if (handle_count < MAX_HANDLES)
        handles[handle_count++].handle = h;
    ext_foreign_toplevel_handle_v1_add_listener(h, &handle_listener, NULL);
}
static void l_finished(void* d, struct ext_foreign_toplevel_list_v1* l) { (void)d; (void)l; }
static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
    .toplevel = l_toplevel,
    .finished = l_finished,
};

static int find_handle(const char* app_id) {
    for (int tries = 0; tries < 50; tries++) {
        for (int i = 0; i < handle_count; i++) {
            if (handles[i].done && !strcmp(handles[i].app_id, app_id))
                return i;
        }
        wl_display_roundtrip(wl_dpy);
    }

    fprintf(stderr, "%s never appeared in the list\n", app_id);
    exit(1);
}

// ---- copy-capture sessions ----
struct session {
    struct ext_image_copy_capture_session_v1* s;
    uint32_t w, h;
    int done, stopped;
};

static void on_buffer_size(void* d, struct ext_image_copy_capture_session_v1* s,
                           uint32_t w, uint32_t h) {
    (void)s;
    struct session* ss = d;
    ss->w = w;
    ss->h = h;
}
static void on_shm_format(void* d, struct ext_image_copy_capture_session_v1* s, uint32_t f) {
    (void)d; (void)s; (void)f;
}
static void on_dmabuf_device(void* d, struct ext_image_copy_capture_session_v1* s,
                             struct wl_array* a) {
    (void)d; (void)s; (void)a;
}
static void on_dmabuf_format(void* d, struct ext_image_copy_capture_session_v1* s,
                             uint32_t f, struct wl_array* m) {
    (void)d; (void)s; (void)f; (void)m;
}
static void on_done(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)s;
    ((struct session*)d)->done++;
}
static void on_stopped(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)s;
    ((struct session*)d)->stopped = 1;
}
static const struct ext_image_copy_capture_session_v1_listener session_listener = {
    .buffer_size = on_buffer_size,
    .shm_format = on_shm_format,
    .dmabuf_device = on_dmabuf_device,
    .dmabuf_format = on_dmabuf_format,
    .done = on_done,
    .stopped = on_stopped,
};

// a session on the window behind handle slot i, run until its constraints
// are complete or it stopped
static void open_session(struct session* ss, int i) {
    struct ext_image_capture_source_v1* source =
        ext_foreign_toplevel_image_capture_source_manager_v1_create_source(tl_source_mgr,
                                                                           handles[i].handle);

    memset(ss, 0, sizeof(*ss));
    ss->s = ext_image_copy_capture_manager_v1_create_session(copy_mgr, source, 0);
    ext_image_copy_capture_session_v1_add_listener(ss->s, &session_listener, ss);

    while (!ss->done && !ss->stopped && wl_display_dispatch(wl_dpy) != -1) {
    }
}

static int frame_result;

static void on_transform(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t t) {
    (void)d; (void)f; (void)t;
}
static void on_damage(void* d, struct ext_image_copy_capture_frame_v1* f,
                      int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)f; (void)x; (void)y; (void)w; (void)h;
}
static void on_ptime(void* d, struct ext_image_copy_capture_frame_v1* f,
                     uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)d; (void)f; (void)hi; (void)lo; (void)ns;
}
static void on_ready(void* d, struct ext_image_copy_capture_frame_v1* f) {
    (void)d; (void)f;
    frame_result = 0x100;
}
static void on_failed(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t r) {
    (void)d; (void)f;
    frame_result = (int)r;
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = on_transform,
    .damage = on_damage,
    .presentation_time = on_ptime,
    .ready = on_ready,
    .failed = on_failed,
};

static struct wl_buffer* xrgb(int w, int h) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("tl-capture-edges", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(2);
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buffer;
}

static int capture(struct session* ss, int w, int h) {
    struct ext_image_copy_capture_frame_v1* frame = ext_image_copy_capture_session_v1_create_frame(ss->s);
    struct wl_buffer* buffer = xrgb(w, h);

    frame_result = -1;
    ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, NULL);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
    ext_image_copy_capture_frame_v1_capture(frame);

    while (frame_result < 0 && wl_display_dispatch(wl_dpy) != -1) {
    }

    ext_image_copy_capture_frame_v1_destroy(frame);
    wl_buffer_destroy(buffer);

    return frame_result;
}

static int expect(const char* what, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: frame result %d, want %d\n", what, got, want);
        exit(1);
    }

    printf("%s: ok\n", what);

    return 0;
}

static void destroy_window(struct wl_toplevel_ctx* c) {
    xdg_toplevel_destroy(c->tl);
    xdg_surface_destroy(c->xs);
    wl_surface_destroy(c->surface);
}

int main(void) {
    const int ready = 0x100;
    const int constraints = EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS;
    const int stopped = EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED;
    const int unknown = EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN;

    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!list || !tl_source_mgr || !copy_mgr) {
        fprintf(stderr, "missing globals\n");
        return 2;
    }

    ext_foreign_toplevel_list_v1_add_listener(list, &list_listener, NULL);

    struct wl_toplevel_ctx grow, doomed, wide;

    wl_make_toplevel(&doomed, "tce-doomed", 180, 120, 0xFF00FF00u);
    wl_make_toplevel(&wide, "tce-wide", 8000, 60, 0xFF0000FFu);
    wl_make_toplevel(&grow, "tce-grow", 200, 150, 0xFFFF0000u);

    int growSlot = find_handle("tce-grow");
    int doomedSlot = find_handle("tce-doomed");
    int wideSlot = find_handle("tce-wide");
    struct session ss;

    // the window grows under the session: the frame sized for the old
    // window bounces with the new size announced, the next one lands
    open_session(&ss, growSlot);
    if (ss.stopped || ss.w != 200 || ss.h != 150) {
        fprintf(stderr, "grow session: stopped=%d %ux%u\n", ss.stopped, ss.w, ss.h);
        return 1;
    }

    wl_surface_attach(grow.surface, wl_solid(240, 150, 0xFFFF0000u), 0, 0);
    wl_surface_damage(grow.surface, 0, 0, 240, 150);
    wl_surface_commit(grow.surface);
    wl_display_roundtrip(wl_dpy);

    int doneBefore = ss.done;

    expect("grown", capture(&ss, 200, 150), constraints);
    if (ss.w != 240 || ss.h != 150 || ss.done == doneBefore) {
        fprintf(stderr, "no fresh constraints after the window grew: %ux%u\n", ss.w, ss.h);
        return 1;
    }
    expect("regrown", capture(&ss, 240, 150), ready);

    // taller only, same width: the height alone is enough to bounce
    wl_surface_attach(grow.surface, wl_solid(240, 180, 0xFFFF0000u), 0, 0);
    wl_surface_damage(grow.surface, 0, 0, 240, 180);
    wl_surface_commit(grow.surface);
    wl_display_roundtrip(wl_dpy);

    expect("taller", capture(&ss, 240, 150), constraints);
    if (ss.w != 240 || ss.h != 180) {
        fprintf(stderr, "no fresh constraints after the window grew taller: %ux%u\n", ss.w, ss.h);
        return 1;
    }
    expect("retaller", capture(&ss, 240, 180), ready);

    // the window unmaps: the session stops, and so does every frame after
    wl_surface_attach(grow.surface, NULL, 0, 0);
    wl_surface_commit(grow.surface);
    wl_display_roundtrip(wl_dpy);

    expect("unmapped", capture(&ss, 240, 150), stopped);
    if (!ss.stopped) {
        fprintf(stderr, "the session on an unmapped window did not stop\n");
        return 1;
    }
    expect("unmapped again", capture(&ss, 240, 150), stopped);
    ext_image_copy_capture_session_v1_destroy(ss.s);

    open_session(&ss, growSlot);
    if (!ss.stopped) {
        fprintf(stderr, "a session opened on an unmapped window did not stop\n");
        return 1;
    }
    ext_image_copy_capture_session_v1_destroy(ss.s);
    printf("unmapped session: ok\n");

    // wider than the output: nothing sane to deliver
    open_session(&ss, wideSlot);
    if (ss.stopped || ss.w != 8000) {
        fprintf(stderr, "wide session: stopped=%d %ux%u\n", ss.stopped, ss.w, ss.h);
        return 1;
    }
    expect("offscreen", capture(&ss, (int)ss.w, (int)ss.h), unknown);
    ext_image_copy_capture_session_v1_destroy(ss.s);

    // a handle outliving its window: the source it yields stops at once
    destroy_window(&doomed);
    while (!handles[doomedSlot].closed && wl_display_dispatch(wl_dpy) != -1) {
    }

    open_session(&ss, doomedSlot);
    if (!ss.stopped) {
        fprintf(stderr, "a dead window's handle opened a live %ux%u session\n", ss.w, ss.h);
        return 1;
    }
    printf("dead handle: ok\n");

    printf("toplevel capture edges done\n");

    return 0;
}
