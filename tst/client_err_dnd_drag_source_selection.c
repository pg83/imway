#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ddm || !wl_seat_g) return 2;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "drag-source-selection", 220, 140, 0xffff0000);
    if (!wlk_enter_serial) return 2;

    struct wl_data_device* dev =
        wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_data_source* src =
        wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_offer(src, "text/plain");
    wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    wl_data_device_set_selection(dev, src, wlk_enter_serial);
    return wl_expect_error(wl_data_source_interface.name,
                           WL_DATA_SOURCE_ERROR_INVALID_SOURCE);
}
