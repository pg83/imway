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

# the picker opens from the glyph the dump locates
glyph_click() {
    local x0 y0 x1 y1
    x0=$(dump_field '^wifi glyph' x0); y0=$(dump_field '^wifi glyph' y0)
    x1=$(dump_field '^wifi glyph' x1); y1=$(dump_field '^wifi glyph' y1)
    [[ "$x0" -ge 0 ]] || return 1
    click_at $(((x0 + x1) / 2)) $(((y0 + y1) / 2))
}

picker_open() {
    [[ -n "$(dump_field '^imgui name=##wifi' x)" ]]
}
picker_closed() {
    [[ -z "$(dump_field '^imgui name=##wifi' x)" ]]
}

open_picker() {
    glyph_click || { echo "no wifi glyph in the bar"; dump_state; exit 1; }
    await 50 picker_open || { echo "the wifi picker did not open"; dump_state; exit 1; }
    sleep 0.3
}

# rows follow the header line and the separator, as in the NetworkManager
# scenario: window padding 8, a 13px line plus 4px spacing, 1px separator
row_click() { # <row index>
    local x y w
    x=$(dump_field '^imgui name=##wifi' x); y=$(dump_field '^imgui name=##wifi' y); w=$(dump_field '^imgui name=##wifi' w)
    click_at $((x + w / 2)) $((y + 30 + $1 * 17 + 6))
}

open_picker

# the top-right "scan" button reaches Station.Scan
x=$(dump_field '^imgui name=##wifi' x); y=$(dump_field '^imgui name=##wifi' y); w=$(dump_field '^imgui name=##wifi' w)
click_at $((x + w - 8 - 14)) $((y + 8 + 6))
await 50 iw "scan requested" || { echo "scan did not reach iwd"; cat "$IWD_LOG"; exit 1; }

# The second network is the one iwd has no key for: connecting to it makes
# the daemon ask our agent, and the answer travels back through the
# passphrase prompt in the picker.
picker_open || open_picker
row_click 1
await 50 iw "connect called /dev0/net_b" || { echo "the network did not connect"; cat "$IWD_LOG"; exit 1; }
sleep 0.5
ctl "type secret"
sleep 0.4
ctl "key 28 press"; ctl "key 28 release"
await 50 iw "passphrase secret" || { echo "the passphrase did not reach the agent"; cat "$IWD_LOG"; exit 1; }

# and the same prompt cancelled: the agent answers with an error
picker_open || open_picker
row_click 1
await 50 test "$(grep -c 'connect called' "$IWD_LOG")" -ge 2 || { echo "the second connect never arrived"; cat "$IWD_LOG"; exit 1; }
sleep 0.5
x=$(dump_field '^imgui name=##wifi' x); y=$(dump_field '^imgui name=##wifi' y)
click_at $((x + 8 + 20)) $((y + 79))
await 50 iw "passphrase refused" || { echo "cancel did not reach the agent"; cat "$IWD_LOG"; exit 1; }

ctl "key 1 press"; ctl "key 1 release"
await 50 picker_closed || { echo "the picker did not close"; dump_state; exit 1; }

expect_alive "compositor died on the fake iwd"
echo "OK: wifi over iwd — object walk, agent passphrase and its cancel, scan, state transition"
