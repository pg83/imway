/* PLAN arithmetic #4: buffer dimensions must divide by buffer_scale after
 * the transform swaps the axes. 64x65 under transform 90 gives 65 wide,
 * which is not divisible by scale 2. */
#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_90);
    wl_surface_set_buffer_scale(surface, 2);
    wl_surface_attach(surface, wl_solid(64, 65, 0xff00ff00), 0, 0);
    wl_surface_commit(surface);

    return wl_expect_error(wl_surface_interface.name, WL_SURFACE_ERROR_INVALID_SIZE);
}
