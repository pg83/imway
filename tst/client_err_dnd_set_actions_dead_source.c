#include "dnd_error.inc"

// the drag source is destroyed once the drop is performed: set_actions on
// its offer has no drag left to negotiate
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc) return rc;
    dnd_auto_accept = 1;
    dnd_error_start(NULL);
    while ((!dnd_entered || !dnd_dropped) && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!dnd_dropped) return 1;
    wl_data_source_destroy(dnd_source);
    dnd_source = NULL;
    wl_data_offer_set_actions(dnd_offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                              WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_OFFER);
}
