#!/usr/bin/env bash
# An interactive move asked for with the press serial after the button went
# up is ignored: moving the pointer afterwards leaves the window in place.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

client="$IMWAY_TESTS_BIN/client_wl_misc"
pos() { echo "$(dump_field "app_id=$1 " x) $(dump_field "app_id=$1 " y)"; }

IMWAY_CLIENT="$client"
start_client stale-move
wait_client "ready"
wait_rect 'app_id=misc-stale-move '
point_at_color 255 0 0 || { echo "window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
# the click reaches the window only once the hover has followed the pointer
# a frame behind: click again, re-aimed, until the client has seen one
clicked() {
    ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_m.ppm"
    ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_m.ppm"
    ctl "button left press"
    ctl "button left release"
    await 10 grep -q "stale move sent" "$CLIENT_LOG"
}
await 20 clicked || { echo "the click never reached the window"; cat "$CLIENT_LOG"; exit 1; }
before=$(pos misc-stale-move)
ctl "motion $((x + 80)) $((y + 60))"; screenshot "$XDG_RUNTIME_DIR/_m.ppm"
ctl "motion $((x + 90)) $((y + 70))"; screenshot "$XDG_RUNTIME_DIR/_m.ppm"
after=$(pos misc-stale-move)
[[ "$before" == "$after" ]] || { echo "a move without a held button moved the window: $before -> $after"; exit 1; }
echo "OK: a move after the button went up is ignored"
