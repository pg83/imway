#!/usr/bin/env bash
# A dismissed xdg_popup stays dismissed: a popup whose grab was refused is
# not mapped by the buffer its client commits anyway; a grab popup dismissed
# by a click outside takes its plain child popup with it, and buffers
# committed to either afterwards map neither; a grabbing child opened on the
# dismissed popup is dismissed at once rather than killing the client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }
none_mapped() { ! dump_state | grep -q '^popup mapped=1'; }
count_mapped() { dump_state | grep -c '^popup mapped=1' || true; }

press_window() {
    wait_client "mapped"
    wait_rect 'app_id=popup-dismissed'
    x=$(( $(dump_field 'app_id=popup-dismissed' imgx) + 250 ))
    y=$(( $(dump_field 'app_id=popup-dismissed' imgy) + 170 ))
    ctl "motion $x $y"
    screenshot "$XDG_RUNTIME_DIR/_h.ppm"
    ctl "motion $((x + 1)) $y"
    screenshot "$XDG_RUNTIME_DIR/_h.ppm"
    ctl "button left press"
    wait_client "grab asked"
    ctl "button left release"
}

# far from the window, the popups and the dock: the empty desktop
click_outside() {
    ctl "motion 1000 600"
    screenshot "$XDG_RUNTIME_DIR/_h.ppm"
    ctl "motion 1001 600"
    screenshot "$XDG_RUNTIME_DIR/_h.ppm"
    ctl "button left press"
    ctl "button left release"
}

# 1: the refused grab's popup, committed anyway
start_client refused
press_window
wait_client "stage refused"
screenshot "$XDG_RUNTIME_DIR/_h.ppm"
none_mapped || { echo "a popup whose grab was refused mapped"; dump_state; exit 1; }
! in_log "imway: popup mapped" || { echo "a popup whose grab was refused mapped"; cat "$IMWAY_LOG"; exit 1; }
go refused
expect_client_ok "the refused-grab client failed"
rm -f "$XDG_RUNTIME_DIR"/go-*

# 2: dismissed by a click outside, then committed again
start_client outside
press_window
wait_client "stage open"
two_mapped() { [[ "$(count_mapped)" == 2 ]]; }
await 50 two_mapped || { echo "the two popups did not map"; dump_state; exit 1; }
go open
click_outside
wait_client "both done"
wait_client "stage recommitted"
screenshot "$XDG_RUNTIME_DIR/_h.ppm"
await 50 none_mapped || { echo "a dismissed popup is still mapped"; dump_state; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_h.ppm"
none_mapped || { echo "a buffer committed to a dismissed popup mapped it again"; dump_state; exit 1; }
go recommitted
expect_client_ok "the outside-click client failed"
rm -f "$XDG_RUNTIME_DIR"/go-*

# 3: a grabbing child of the dismissed popup
start_client nested
press_window
wait_client "stage open"
await 50 two_mapped || { echo "the two popups did not map"; dump_state; exit 1; }
go open
click_outside
wait_client "stage nested"
none_mapped || { echo "a grabbing child of a dismissed popup mapped"; dump_state; exit 1; }
go nested
expect_client_ok "a grabbing child of a dismissed popup killed the client"

expect_alive "compositor died dismissing popups"
echo "OK: dismissed popups stay dismissed and take their children along"
