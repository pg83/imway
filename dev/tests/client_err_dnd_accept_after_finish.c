#include "dnd_error.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_finished_boot();
    if (rc) return rc;
    wl_data_offer_accept(dnd_offer, 0, "text/plain");
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_OFFER);
}
