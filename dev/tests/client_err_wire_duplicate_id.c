#include "wire_error.inc"

int main(void) {
    alarm(10);
    wire_boot();
    uint32_t msg[] = {1, 12u << 16, 1};
    wire_send(msg, 3);
    return wire_wait_closed();
}
