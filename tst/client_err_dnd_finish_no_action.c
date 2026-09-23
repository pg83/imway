#include "dnd_error.inc"

// the source never sets its actions, so the drop is a copy nobody
// negotiated and the offer never gets an action: finish is refused
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
    wl_data_device_start_drag(dnd_device, dnd_source, dnd_top.surface, NULL, wlp_button_serial);
    printf("dragging\n");
    while (!dnd_entered && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_data_offer_accept(dnd_offer, 0, "text/plain");
    printf("entered\n");
    while (!dnd_dropped && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_data_offer_finish(dnd_offer);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_FINISH);
}
