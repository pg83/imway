#include "dnd_error.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc || !wlk_enter_serial) return rc ? rc : 2;

    dnd_source = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(dnd_source, &source_listener, NULL);
    wl_data_source_offer(dnd_source, "text/plain");
    wl_data_device_set_selection(dnd_device, dnd_source, wlk_enter_serial);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("dragging\n");
    wl_data_device_start_drag(dnd_device, dnd_source, dnd_top.surface, NULL,
                              wlp_button_serial);
    return wl_expect_error(wl_data_device_interface.name,
                           WL_DATA_DEVICE_ERROR_USED_SOURCE);
}
