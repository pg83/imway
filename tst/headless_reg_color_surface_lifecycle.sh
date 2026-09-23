#!/usr/bin/env bash
# A color-management surface object unset with nothing set, destroyed before
# any set, replaced by a fresh one, and destroyed after its wl_surface: the
# client keeps its connection through all of it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "color surface lifecycle done"
expect_client_ok "a color-management surface object cost the client its connection"
expect_alive "compositor died over a color-management surface's life"
echo "OK: the color-management surface object's lifecycle is error free"
