#include "dmabuf_error.inc"

int main(void) {
    alarm(10);
    int rc = dmabuf_test_boot();
    if (rc) return rc;

    struct zwp_linux_buffer_params_v1* p = dmabuf_test_params();

    /* dmabuf_error.inc supplies a 4096-byte fd; this layout needs 65536. */
    dmabuf_test_add(p, 0, 0, 512, dmabuf_test_mod_hi, dmabuf_test_mod_lo);
    zwp_linux_buffer_params_v1_create_immed(p, 128, 128,
                                            dmabuf_test_format, 0);

    return wl_expect_error(zwp_linux_buffer_params_v1_interface.name,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS);
}
