#include "dnd_error.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    int rc = dnd_error_boot();
    if (rc) return rc;

    struct wl_toplevel_ctx other;
    wl_make_toplevel(&other, "dnd-other-origin", 180, 120, 0xff00ff00);
    printf("origins ready\n");
    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }

    dnd_source = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(dnd_source, &source_listener, NULL);
    wl_data_source_offer(dnd_source, "text/plain");
    struct wl_surface* wrong =
        wlp_focus == dnd_top.surface ? other.surface : dnd_top.surface;
    wl_data_device_start_drag(dnd_device, dnd_source, wrong, NULL,
                              wlp_button_serial);
    wl_display_flush(wl_dpy);
    while (!dnd_cancelled && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!dnd_cancelled) return 1;
    printf("wrong surface cancelled\n");
    return 0;
}
