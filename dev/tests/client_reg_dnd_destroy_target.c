#include "dnd_error.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc) return rc;
    dnd_error_start(NULL);
    while (!dnd_entered && wl_display_dispatch(wl_dpy) != -1) {
    }
    xdg_toplevel_destroy(dnd_top.tl);
    xdg_surface_destroy(dnd_top.xs);
    wl_surface_destroy(dnd_top.surface);
    wl_display_flush(wl_dpy);
    printf("torn down\n");
    while (!dnd_cancelled && wl_display_dispatch(wl_dpy) != -1) {
    }
    return dnd_cancelled ? 0 : 1;
}
