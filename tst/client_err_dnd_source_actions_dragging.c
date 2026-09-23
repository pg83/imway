#include "dnd_error.inc"

// a source's actions are set before its drag starts or never: set_actions
// on a source already dragging is invalid even when it never set any
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc) return rc;
    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }
    dnd_source = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(dnd_source, &source_listener, NULL);
    wl_data_source_offer(dnd_source, "text/plain");
    printf("dragging\n");
    wl_data_device_start_drag(dnd_device, dnd_source, dnd_top.surface, NULL, wlp_button_serial);
    wl_data_source_set_actions(dnd_source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    return wl_expect_error(wl_data_source_interface.name,
                           WL_DATA_SOURCE_ERROR_INVALID_SOURCE);
}
