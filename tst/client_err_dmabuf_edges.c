// linux-dmabuf params that fail create_immed on one more count each: a
// width with no height, a height past the renderer's image limit, planes
// that skip plane 0, planes added out of order to more than the format
// has, and a plane whose fd is a pipe (no size to check, and not a
// dma-buf either). Exit 77 when the compositor offers no dmabuf format.

#include "dmabuf_error.inc"

int main(int argc, char** argv) {
    alarm(10);

    if (argc != 2) return 2;

    int rc = dmabuf_test_boot();

    if (rc) return rc;

    const char* mode = argv[1];
    struct zwp_linux_buffer_params_v1* p = dmabuf_test_params();
    uint32_t hi = dmabuf_test_mod_hi, lo = dmabuf_test_mod_lo;

    if (!strcmp(mode, "flat")) {
        dmabuf_test_add(p, 0, 0, 16, hi, lo);
        zwp_linux_buffer_params_v1_create_immed(p, 4, 0, dmabuf_test_format, 0);
        return wl_expect_error(zwp_linux_buffer_params_v1_interface.name, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS);
    }

    if (!strcmp(mode, "too-tall")) {
        dmabuf_test_add(p, 0, 0, 16, hi, lo);
        zwp_linux_buffer_params_v1_create_immed(p, 1, 1 << 20, dmabuf_test_format, 0);
        return wl_expect_error(zwp_linux_buffer_params_v1_interface.name, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS);
    }

    if (!strcmp(mode, "no-plane-zero")) {
        dmabuf_test_add(p, 1, 0, 16, hi, lo);
        zwp_linux_buffer_params_v1_create_immed(p, 4, 1, dmabuf_test_format, 0);
        return wl_expect_error(zwp_linux_buffer_params_v1_interface.name, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE);
    }

    if (!strcmp(mode, "planes-reversed")) {
        // plane 1 first: plane 0 after it must not shrink the plane count,
        // and two planes are one too many for the single-plane format
        dmabuf_test_add(p, 1, 0, 16, hi, lo);
        dmabuf_test_add(p, 0, 0, 16, hi, lo);
        zwp_linux_buffer_params_v1_create_immed(p, 4, 1, dmabuf_test_format, 0);
        return wl_expect_error(zwp_linux_buffer_params_v1_interface.name, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE);
    }

    if (!strcmp(mode, "pipe-plane")) {
        int fds[2];

        if (pipe(fds) < 0) return 2;

        zwp_linux_buffer_params_v1_add(p, fds[0], 0, 0, 16, hi, lo);
        close(fds[0]);
        close(fds[1]);
        zwp_linux_buffer_params_v1_create_immed(p, 4, 1, dmabuf_test_format, 0);
        return wl_expect_error(zwp_linux_buffer_params_v1_interface.name, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER);
    }

    return 2;
}
