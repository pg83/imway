#include "dnd_error.inc"

// a drop accepted on enter, then un-accepted with a null mime type: the
// finish that follows has nothing accepted to finish
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
    wl_data_offer_accept(dnd_offer, 0, NULL);
    wl_data_offer_finish(dnd_offer);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_FINISH);
}
