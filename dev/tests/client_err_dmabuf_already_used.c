#include "dmabuf_error.inc"

static int failed;

static void created(void* d, struct zwp_linux_buffer_params_v1* p, struct wl_buffer* buffer) {
    (void)d; (void)p; (void)buffer;
}
static void failed_event(void* d, struct zwp_linux_buffer_params_v1* p) {
    (void)d; (void)p;
    failed = 1;
}
static const struct zwp_linux_buffer_params_v1_listener params_listener = {
    created, failed_event,
};

int main(void) {
    alarm(10);
    int rc = dmabuf_test_boot();
    if (rc) return rc;
    struct zwp_linux_buffer_params_v1* p = dmabuf_test_params();
    zwp_linux_buffer_params_v1_add_listener(p, &params_listener, NULL);
    dmabuf_test_add(p, 0, 0, 4, dmabuf_test_mod_hi, dmabuf_test_mod_lo);
    zwp_linux_buffer_params_v1_create(p, 1, 1, dmabuf_test_format, 0);
    if (wl_display_roundtrip(wl_dpy) < 0 || !failed) return 1;
    dmabuf_test_add(p, 0, 0, 4, dmabuf_test_mod_hi, dmabuf_test_mod_lo);
    return wl_expect_error(zwp_linux_buffer_params_v1_interface.name,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED);
}
