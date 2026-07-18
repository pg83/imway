#include "dnd_error.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc) return rc;
    dnd_error_start(NULL);
    uint32_t both = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                    WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
    wl_data_offer_set_actions(dnd_offer, both, both);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_ACTION);
}
