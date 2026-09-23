#include "selection_offer_error.inc"

// finish belongs to a drag-and-drop offer; a clipboard offer has no drop
// to finish
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = selection_offer_boot();
    if (rc) return rc;
    wl_data_offer_finish(selection_offer);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_FINISH);
}
