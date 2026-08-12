#include "dnd_error.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_finished_boot();
    if (rc) return rc;

    int fds[2];
    if (pipe(fds) < 0) return 1;
    wl_data_offer_receive(dnd_offer, "text/plain", fds[1]);
    close(fds[1]);
    close(fds[0]);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_OFFER);
}
