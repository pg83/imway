#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct xdg_positioner* p = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_gravity(p, 0xdeadbeef);

    return wl_expect_error(xdg_positioner_interface.name,
                           XDG_POSITIONER_ERROR_INVALID_INPUT);
}
