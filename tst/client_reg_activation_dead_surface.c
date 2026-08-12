#include "activation_serial.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(15);
    if (activation_boot()) return 1;
    struct wl_toplevel_ctx first, second;
    wl_make_toplevel(&first, "activation-dead-target", 200, 130, 0xffff0000);
    wl_make_toplevel(&second, "activation-dead-focus", 200, 130, 0xff00ff00);
    uint32_t serial = wlk_enter_serial;
    struct wl_surface* dead = wl_compositor_create_surface(wl_comp);
    struct xdg_activation_token_v1* token = request_token(serial, dead);
    wl_surface_destroy(dead);
    xdg_activation_token_v1_commit(token);
    if (wait_token()) return 1;
    xdg_activation_v1_activate(activation, activation_token, first.surface);
    if (wl_display_roundtrip(wl_dpy) < 0 || wl_display_roundtrip(wl_dpy) < 0) return 1;
    printf("dead-surface activation attempted\n");
    return wlk_focus == second.surface ? 0 : 1;
}
