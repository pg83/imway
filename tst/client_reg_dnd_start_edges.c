// wl_data_device.start_drag off the beaten path:
//   a serial that is not the button's cancels the source and starts nothing,
//   and the same without a source starts nothing either;
//   a drag icon surface keeps its role, and a second drag takes it again.

#include "dnd_error.inc"

static struct wl_data_source* fresh_source(void) {
    struct wl_data_source* s = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(s, &source_listener, NULL);
    wl_data_source_offer(s, "text/plain");
    return s;
}

static int drag_with_icon(struct wl_surface* icon, int round) {
    struct wl_data_source* src = fresh_source();

    dnd_entered = 0;
    dnd_cancelled = 0;
    wl_data_device_start_drag(dnd_device, src, dnd_top.surface, icon, wlp_button_serial);
    printf("dragging %d\n", round);
    while (!dnd_entered && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("entered %d\n", round);
    // nothing accepted the offer: letting go cancels the drag
    while (!dnd_cancelled && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_data_source_destroy(src);
    return dnd_cancelled ? 0 : 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    int rc = dnd_error_boot();
    if (rc) return rc;

    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }
    uint32_t stale = wlp_button_serial + 1;

    struct wl_data_source* refused = fresh_source();
    wl_data_device_start_drag(dnd_device, refused, dnd_top.surface, NULL, stale);
    while (!dnd_cancelled && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_data_source_destroy(refused);

    wl_data_device_start_drag(dnd_device, NULL, dnd_top.surface, NULL, stale);
    if (wl_display_roundtrip(wl_dpy) < 0 || dnd_entered) {
        fprintf(stderr, "a drag started on a serial that is not the button's\n");
        return 1;
    }
    printf("wrong serial refused\n");

    struct wl_surface* icon = wl_compositor_create_surface(wl_comp);

    if (drag_with_icon(icon, 1)) return 1;

    int presses = wlp_button_count;
    while ((wlp_button_count == presses || wlp_button_state != WL_POINTER_BUTTON_STATE_PRESSED) && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (drag_with_icon(icon, 2)) return 1;

    printf("icon reused\n");
    return 0;
}
