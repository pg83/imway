#include "dnd_error.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc) return rc;
    dnd_error_start(NULL);
    wl_data_offer_finish(dnd_offer);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_FINISH);
}
