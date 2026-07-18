#include "dnd_error.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc) return rc;
    struct wl_surface* icon = wl_compositor_create_surface(wl_comp);
    dnd_error_start(icon);
    while (!dnd_entered && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_surface_destroy(icon);
    wl_display_flush(wl_dpy);
    printf("torn down\n");
    while (!dnd_cancelled && wl_display_dispatch(wl_dpy) != -1) {
    }
    return dnd_cancelled ? 0 : 1;
}
