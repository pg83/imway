#include "dnd_error.inc"

// set_actions on an offer the client has finished is refused
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_finished_boot();
    if (rc) return rc;
    wl_data_offer_set_actions(dnd_offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                              WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_OFFER);
}
