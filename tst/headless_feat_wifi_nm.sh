#!/usr/bin/env bash
# private-session-bus
# imway-pre: "$IMWAY_CLIENT" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# Wifi over a fake NetworkManager on the aliased system bus, iwd absent: the
# compositor walks Devices, the saved connections and every access point,
# the picker opens from the bar glyph, a known network activates through its
# saved connection, an unknown secured one asks for the passphrase and goes
# through AddAndActivateConnection, an open one activates without any, the
# scan button reaches RequestScan, and the bar and a toast follow the state.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

NM_LOG="$XDG_RUNTIME_DIR/nm.log"

nm() { grep -q "$1" "$NM_LOG"; }

await 150 in_log "wifi via NetworkManager" || { echo "NetworkManager was not detected"; cat "$IMWAY_LOG"; exit 1; }
await 150 nm "get /org/freedesktop/NetworkManager org.freedesktop.NetworkManager Devices" || { echo "Devices never read"; cat "$NM_LOG"; exit 1; }
await 150 nm "settings served" || { echo "the saved connection was never read"; cat "$NM_LOG"; exit 1; }
await 150 nm "get-all /org/freedesktop/NetworkManager/AccessPoint/3" || { echo "access points were not walked"; cat "$NM_LOG"; exit 1; }

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

# rows follow the header line and the separator at the default 13px font:
# window padding 8, a 13px line plus 4px spacing, a 1px separator plus 4px
row_click() { # <row index>
    local x y w
    x=$(dump_field '^imgui name=##wifi' x); y=$(dump_field '^imgui name=##wifi' y); w=$(dump_field '^imgui name=##wifi' w)
    click_at $((x + w / 2)) $((y + 30 + $1 * 17 + 6))
}

open_picker

# the top-right "scan" button
x=$(dump_field '^imgui name=##wifi' x); y=$(dump_field '^imgui name=##wifi' y); w=$(dump_field '^imgui name=##wifi' w)
click_at $((x + w - 8 - 14)) $((y + 8 + 6))
await 150 nm "scan requested" || { echo "scan did not reach NetworkManager"; cat "$NM_LOG"; exit 1; }

# row 0: the known network activates through its saved connection
picker_open || open_picker
row_click 0
await 150 nm "activate /org/freedesktop/NetworkManager/Settings/1 /org/freedesktop/NetworkManager/AccessPoint/1" || {
    echo "the known network did not activate through its connection"; cat "$NM_LOG"; exit 1; }
sleep 0.5

# the glyph shrinks from "wifi off" to "wifi" once the refresh commits the
# activated state
glyph_narrowed() {
    local x0 x1
    x0=$(dump_field '^wifi glyph' x0); x1=$(dump_field '^wifi glyph' x1)
    [[ $((x1 - x0)) -lt 40 ]]
}
await 100 glyph_narrowed || { echo "the bar glyph did not follow the activation"; dump_state; exit 1; }

# row 2: unknown and secured — the passphrase prompt takes over the dialog
picker_open || open_picker
row_click 2
sleep 0.5
ctl "type secret"
sleep 0.4
ctl "key 28 press"; ctl "key 28 release"
await 150 nm "add-activate imway-secured psk=secret" || { echo "the passphrase did not reach AddAndActivateConnection"; cat "$NM_LOG"; exit 1; }

# row 1: open network, no passphrase, no saved connection
picker_open || open_picker
row_click 1
await 150 nm "activate / /org/freedesktop/NetworkManager/AccessPoint/2" || { echo "the open network did not activate"; cat "$NM_LOG"; exit 1; }

# the prompt again, this time cancelled: the cancel button sits under the
# header, the prompt line and the input field; Escape then closes the list
picker_open || open_picker
row_click 2
sleep 0.5
x=$(dump_field '^imgui name=##wifi' x); y=$(dump_field '^imgui name=##wifi' y)
click_at $((x + 8 + 20)) $((y + 79))
sleep 0.3
ctl "key 1 press"; ctl "key 1 release"
await 50 picker_closed || { echo "cancel did not return the picker to its list"; dump_state; exit 1; }
[[ "$(grep -c add-activate "$NM_LOG")" -eq 1 ]] || { echo "cancel still activated something"; cat "$NM_LOG"; exit 1; }

expect_alive "compositor died on the fake NetworkManager"
echo "OK: wifi over NetworkManager — refresh tree, activation, passphrase, scan"
