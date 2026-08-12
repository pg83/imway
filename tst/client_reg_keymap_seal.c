// Regression (#2): the wl_keyboard keymap fd is shared with every client and
// wl_seat v5 lets them MAP_SHARED it, so it must be sealed against writes and
// truncation. Bind a keyboard, take the keymap fd, and check F_SEAL_WRITE is
// set. Prints "sealed" / "unsealed" and exits 0 only when sealed.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    if (!wl_kbd) {
        fprintf(stderr, "client_reg_keymap_seal: no keyboard capability\n");
        return 1;
    }

    // the keymap event arrives right after get_keyboard; boot already did one
    // roundtrip after seat caps, do another to be sure it landed
    wl_display_roundtrip(wl_dpy);

    if (wlk_keymap_fd < 0) {
        fprintf(stderr, "client_reg_keymap_seal: no keymap delivered\n");
        return 1;
    }

    int seals = fcntl(wlk_keymap_fd, F_GET_SEALS);
    if (seals < 0) {
        perror("F_GET_SEALS");
        return 1;
    }

    printf("client_reg_keymap_seal: seals=0x%x write=%d shrink=%d\n", seals,
           !!(seals & F_SEAL_WRITE), !!(seals & F_SEAL_SHRINK));

    if (!(seals & F_SEAL_WRITE) || !(seals & F_SEAL_SHRINK)) {
        printf("client_reg_keymap_seal: unsealed\n");
        return 1;
    }

    printf("client_reg_keymap_seal: sealed\n");
    return 0;
}
