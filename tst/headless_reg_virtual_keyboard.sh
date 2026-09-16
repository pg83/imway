#!/usr/bin/env bash
# zwp_virtual_keyboard_v1: a synthesized key travels through the seat and is
# delivered to the focused surface like a physical one.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "client_reg_virtual_keyboard: typed"
expect_client_ok "the virtual keyboard key did not reach the focused surface"
expect_alive "compositor died driving a virtual keyboard"
echo "OK: zwp_virtual_keyboard_v1 keys reach the focused surface"
