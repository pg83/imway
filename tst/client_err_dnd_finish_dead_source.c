#include "dnd_error.inc"

#include <poll.h>

// the source is destroyed once the drop is performed: a receive on its
// offer hands back a pipe nobody writes (the read end sees EOF at once),
// an accept goes nowhere, and finish, with no source to tell, is refused
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

    int fds[2];
    if (pipe(fds) < 0) return 2;
    wl_data_offer_receive(dnd_offer, "text/plain", fds[1]);
    close(fds[1]);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "receive on a dead source's offer was refused\n");
        return 1;
    }
    struct pollfd pfd = {fds[0], POLLIN, 0};
    char byte;
    if (poll(&pfd, 1, 5000) != 1 || read(fds[0], &byte, 1) != 0) {
        fprintf(stderr, "the dead source's pipe did not end at once\n");
        return 1;
    }
    close(fds[0]);

    wl_data_offer_accept(dnd_offer, 0, "text/plain");
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "accept on a dead source's offer was refused\n");
        return 1;
    }
    wl_data_offer_finish(dnd_offer);
    return wl_expect_error(wl_data_offer_interface.name,
                           WL_DATA_OFFER_ERROR_INVALID_FINISH);
}
