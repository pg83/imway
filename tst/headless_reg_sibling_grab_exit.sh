#!/usr/bin/env bash
# A client with two grab popups of one toplevel destroys the first popup's
# surface while the second tops the grab stack (the top popup keeps the
# keyboard), then exits; the compositor must unwind the stack and route
# pointer and keyboard to the next client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "sibling-grab-exit mapped"
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_hover.ppm" # a frame computes hover before the click
ctl "button left press"
wait_client "sibling-grab-exit both grabbed"
ctl "button left release"
expect_client_ok "the top grab popup lost the keyboard"

grabs_gone() {
    [[ -z "$(dump_state | grep '^popup ')" ]]
}
await 100 grabs_gone || { echo "the exited client's popups stayed in the scene"; dump_state; exit 1; }

input_health_probe || exit 1
expect_alive "compositor died unwinding a dead client's grab stack"
echo "OK: a buried grab popup dies quietly, the stack unwinds and input reaches the next client"
