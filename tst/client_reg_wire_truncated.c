#include "wire_error.inc"

int main(void) {
    wire_boot();
    uint32_t msg[] = {1, 64u << 16, 2};
    wire_send(msg, 3);
    wl_display_disconnect(wire_display);
    return 0;
}
