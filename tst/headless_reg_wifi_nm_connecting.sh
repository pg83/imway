#!/usr/bin/env bash
# private-session-bus
# imway-env: XDG_DATA_HOME=./xdg
# imway-pre: mkdir -p xdg/icons/hicolor/scalable/apps && printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#00ff00"/></svg>' > xdg/icons/hicolor/scalable/apps/network-wireless.svg
# imway-pre: NM_STATE=50 "$IMWAY_TESTS_BIN/client_feat_wifi_nm" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# NetworkManager caught mid-connect at startup: wifi reads as connecting and
# the bar's glyph says so, wider than the plain one it narrows to once a
# network is up. The picker opened over it closes on a click elsewhere. The
# connection's notification keeps its icon in the history.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

NM_LOG="$XDG_RUNTIME_DIR/nm.log"
nm() { grep -q "$1" "$NM_LOG"; }
wifi_state() { dump_field '^wifi state' state; }
glyph_w() { echo $(( $(dump_field '^wifi glyph' x1) - $(dump_field '^wifi glyph' x0) )); }

await 150 in_log "wifi via NetworkManager" || { echo "NetworkManager was not detected"; cat "$IMWAY_LOG"; exit 1; }
await 150 eval '[[ "$(wifi_state)" == 2 ]]' || { echo "a device in NM's config state is not connecting: $(dump_state | grep '^wifi')"; cat "$NM_LOG"; exit 1; }
await 150 nm "get-all /org/freedesktop/NetworkManager/AccessPoint/3" || { echo "access points were not walked"; cat "$NM_LOG"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_settle.ppm"
connecting_w=$(glyph_w)

glyph_click() {
    local x0 y0 x1 y1
    x0=$(dump_field '^wifi glyph' x0); y0=$(dump_field '^wifi glyph' y0)
    x1=$(dump_field '^wifi glyph' x1); y1=$(dump_field '^wifi glyph' y1)
    click_at $(((x0 + x1) / 2)) $(((y0 + y1) / 2))
}
picker_open() { [[ -n "$(dump_field '^imgui name=##wifi' x)" ]]; }
picker_closed() { [[ -z "$(dump_field '^imgui name=##wifi' x)" ]]; }

glyph_click
await 50 picker_open || { echo "the wifi picker did not open"; dump_state; exit 1; }
sleep 0.3
click_at 600 500
await 50 picker_closed || { echo "a click elsewhere left the picker open"; dump_state; exit 1; }

# connect the known network (row 0) and the glyph narrows
glyph_click
await 50 picker_open || { echo "the wifi picker did not reopen"; dump_state; exit 1; }
sleep 0.3
x=$(dump_field '^imgui name=##wifi' x); y=$(dump_field '^imgui name=##wifi' y); w=$(dump_field '^imgui name=##wifi' w)
click_at $((x + w / 2)) $((y + 30 + 6))
await 150 nm "activate /org/freedesktop/NetworkManager/Settings/1" || { echo "the known network did not activate"; cat "$NM_LOG"; exit 1; }
await 150 eval '[[ "$(wifi_state)" == 3 ]]' || { echo "wifi did not read as connected"; dump_state | grep '^wifi'; exit 1; }
narrowed() { (( $(glyph_w) < connecting_w )); }
await 100 narrowed || { echo "the connecting glyph ($connecting_w) is not wider than the connected one ($(glyph_w))"; exit 1; }

# the history lists the connection with the wireless icon beside it; do not
# disturb first takes the toast, which wears the same icon, off the screen
await 50 eval '[[ "$(dump_field "^notifications " history)" -ge 1 ]]' || { echo "the connection was not notified"; dump_state; exit 1; }
ctl "set notifications.dnd true"
await 50 eval '[[ -z "$(dump_field "^imgui name=##toast" x)" ]]' || { echo "a toast stayed up under do not disturb"; exit 1; }
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type notifications"
await_input "notifications" || { echo "the query did not land"; exit 1; }
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui '##history' || { echo "the history did not open"; dump_state; exit 1; }
icon_in_history() {
    local hx hy hw hh
    hx=$(dump_field '^imgui name=##history ' x); hy=$(dump_field '^imgui name=##history ' y)
    hw=$(dump_field '^imgui name=##history ' w); hh=$(dump_field '^imgui name=##history ' h)
    screenshot "$XDG_RUNTIME_DIR/_hist.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/_hist.ppm" "$hx" "$hy" "$hw" "$hh" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); px = f.read(w * h * 3)
x0, y0, ww, hh = map(int, sys.argv[2:6])
n = sum(1 for y in range(y0, min(h, y0 + hh)) for x in range(x0, min(w, x0 + ww)) if px[(y * w + x) * 3:(y * w + x) * 3 + 3] == b'\x00\xff\x00')
sys.exit(0 if n >= 64 else 1)
PY
}
await 50 icon_in_history || { echo "the history shows no icon for the wi-fi notification"; exit 1; }

expect_alive "compositor died on a connecting NetworkManager"
echo "OK: a connecting device shows as such, the picker closes on an outside click"
