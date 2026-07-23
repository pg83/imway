#!/usr/bin/env bash
# private-session-bus
# imway-pre: "$IMWAY_CLIENT" >"$XDG_RUNTIME_DIR/iwd.log" 2>&1 & for i in $(seq 50); do grep -q "iwd ready" "$XDG_RUNTIME_DIR/iwd.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake iwd did not come up"; exit 1
# Wifi over a fake iwd on the aliased system bus: the compositor detects it,
# walks GetManagedObjects/GetOrderedNetworks, registers its agent, and the
# bar glyph follows the PropertiesChanged state flip.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IWD_LOG="$XDG_RUNTIME_DIR/iwd.log"

iw() { grep -q "$1" "$IWD_LOG"; }

await 50 in_log "wifi via iwd" || { echo "iwd was not detected"; cat "$IMWAY_LOG"; exit 1; }
await 50 iw "managed objects served" || { echo "GetManagedObjects never arrived"; cat "$IWD_LOG"; exit 1; }
await 50 iw "ordered served" || { echo "GetOrderedNetworks never arrived"; cat "$IWD_LOG"; exit 1; }
await 50 iw "agent registered" || { echo "agent was not registered"; cat "$IWD_LOG"; exit 1; }

# disconnected: the bar shows "wifi off" at the right edge
screenshot "$XDG_RUNTIME_DIR/before.ppm"

await 60 iw "flipped connected" || { echo "fake never flipped"; cat "$IWD_LOG"; exit 1; }

# the glyph shrinks to "wifi": the bar right of x=980 must change
glyph_changed() {
    screenshot "$XDG_RUNTIME_DIR/after.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" 980 2 1195 20)" -gt 30 ]]
}

await 50 glyph_changed || { echo "wifi glyph did not follow the state change"; exit 1; }

expect_alive "compositor died on the fake iwd"
echo "OK: wifi over iwd — detection, object walk, agent, state transition"
