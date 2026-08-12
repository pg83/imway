#include "activation_serial.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(15);
    if (activation_boot()) return 1;
    struct wl_toplevel_ctx first, second;
    wl_make_toplevel(&first, "activation-reuse-first", 200, 130, 0xffff0000);
    wl_make_toplevel(&second, "activation-reuse-second", 200, 130, 0xff00ff00);
    struct xdg_activation_token_v1* token = request_token(wlk_enter_serial, second.surface);
    xdg_activation_token_v1_commit(token);
    if (wait_token()) return 1;
    xdg_activation_v1_activate(activation, activation_token, first.surface);
    for (int i = 0; i < 3 && wl_display_roundtrip(wl_dpy) >= 0; i++) {}
    if (wlk_focus != first.surface) return 1;
    xdg_activation_v1_activate(activation, activation_token, second.surface);
    for (int i = 0; i < 3 && wl_display_roundtrip(wl_dpy) >= 0; i++) {}
    printf("activation token reused\n");
    return wlk_focus == first.surface ? 0 : 1;
}
