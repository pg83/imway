#include "dnd_error.inc"

// a source already dragging cannot become the selection as well
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc) return rc;
    dnd_error_start(NULL);
    wl_data_device_set_selection(dnd_device, dnd_source, wlp_button_serial);
    return wl_expect_error(wl_data_device_interface.name,
                           WL_DATA_DEVICE_ERROR_USED_SOURCE);
}
