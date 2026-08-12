#include "dmabuf_error.inc"

int main(void) {
    alarm(10);
    int rc = dmabuf_test_boot();
    if (rc) return rc;
    struct zwp_linux_buffer_params_v1* p = dmabuf_test_params();
    dmabuf_test_add(p, 0, 0, 4, 0, 0);
    dmabuf_test_add(p, 1, 0, 4, 0, 1);
    return wl_expect_error(zwp_linux_buffer_params_v1_interface.name,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT);
}
