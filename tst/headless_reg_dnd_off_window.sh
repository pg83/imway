#!/usr/bin/env bash
# A drag carried off the window over the bare desktop and back: the window
# gets a leave and then a fresh enter. A second button pressed and released
# mid-drag leaves the drag running (it still leaves the window again); the
# release of the button that started it ends it, and with nothing accepted
# the source is cancelled.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "off-window ready"
wait_rect 'app_id=dnd-off-window'
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.3

ctl "button left press"
wait_client "drag started"

x=$(dump_field 'app_id=dnd-off-window' imgx)
y=$(dump_field 'app_id=dnd-off-window' imgy)
w=$(dump_field 'app_id=dnd-off-window' client_w)
h=$(dump_field 'app_id=dnd-off-window' client_h)
inx=$((x + w / 2)); iny=$((y + h / 2))
if (( x + w + 100 < 1250 )); then offx=$((x + w + 100)); else offx=$((x - 100)); fi
(( offx > 80 )) || { echo "no bare desktop beside the window at $x,$y ${w}x$h"; exit 1; }

# picking follows the hover of the last rendered frame: aim, let a frame
# render, aim again, until the client reports the crossing
aim_until() { # <x> <y> <client line>
    for _ in $(seq 1 20); do
        ctl "motion $1 $2"
        screenshot "$XDG_RUNTIME_DIR/_aim.ppm"
        ctl "motion $(($1 + 1)) $2"
        grep -q "^$3\$" "$CLIENT_LOG" && return 0
        sleep 0.2
    done
    wait_client "$3"
}

aim_until $inx $iny "entered 1"
aim_until $offx $iny "left 1"
aim_until $((inx + 5)) $iny "entered 2"

ctl "button right press"
ctl "button right release"
aim_until $offx $((iny + 5)) "left 2"

ctl "button left release"
wait_client "source cancelled"
wait_client "off-window done entered=2 left=2"
expect_client_ok "the drag off the window went wrong"
expect_alive "compositor died dragging off the window"
input_health_probe
echo "OK: a drag leaves and re-enters the window and survives a second button"
