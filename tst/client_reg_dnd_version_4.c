// wl_data_device_manager v4: the compositor must advertise version >= 4 and
// accept the v4 release request (a destructor on the manager).

#define REG_DDM_VERSION 4
#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    if (!wl_ddm) {
        fprintf(stderr, "no data_device_manager\n");
        return 1;
    }
    if (wl_data_device_manager_get_version(wl_ddm) < 4) {
        fprintf(stderr, "data_device_manager at version %u, want >= 4\n",
                wl_data_device_manager_get_version(wl_ddm));
        return 1;
    }

    // exercise the v4 destructor, then make sure the connection is still sane
    wl_data_device_manager_release(wl_ddm);
    wl_ddm = NULL;
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "release killed the connection\n");
        return 1;
    }

    printf("client_reg_dnd_version_4: ok\n");
    return 0;
}
