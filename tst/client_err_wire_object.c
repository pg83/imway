#include "wire_error.inc"

int main(void) {
    alarm(10);
    wire_boot();
    uint32_t msg[] = {0xdeadbeef, 8u << 16};
    wire_send(msg, 2);
    return wire_wait_closed();
}
