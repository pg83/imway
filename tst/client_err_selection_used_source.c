#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ddm || !wl_seat_g) return 2;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "selection-used", 200, 120, 0xff00ff00);
    if (!wlk_enter_serial) return 2;
    struct wl_data_device* device =
        wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_data_source* source =
        wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_offer(source, "text/plain");
    wl_data_device_set_selection(device, source, wlk_enter_serial);
    wl_data_device_set_selection(device, source, wlk_enter_serial);
    return wl_expect_error(wl_data_device_interface.name,
                           WL_DATA_DEVICE_ERROR_USED_SOURCE);
}
