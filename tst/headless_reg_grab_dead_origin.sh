#!/usr/bin/env bash
# A press whose surface (a subsurface a drag started from) is destroyed
# while the button is still held: the move and the popup grab its client
# then asks for with the press's serial are refused, since the press no
# longer belongs to any window. The window stays put under the dragging
# pointer, the popup is dismissed, and the compositor keeps serving input.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "dead origin ready"
wait_rect 'app_id=grab-dead-origin'
wait_placed 'app_id=grab-dead-origin' || { echo "the window never settled"; exit 1; }

x0=$(dump_field 'app_id=grab-dead-origin' x); y0=$(dump_field 'app_id=grab-dead-origin' y)
point_at_color 0 0 255 || { echo "the subsurface is not on screen"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 0 0 255)
# pointer focus is worked out from a rendered frame: aim until the client
# says the pointer is on the subsurface
for _ in $(seq 1 20); do
    ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_aim.ppm"
    ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_aim.ppm"
    grep -q "pointer on the subsurface" "$CLIENT_LOG" && break
done
wait_client "pointer on the subsurface"
ctl "button left press"
wait_client "dead-origin move requested"

ctl "motion $((x + 80)) $((y + 60))"
ctl "motion $((x + 120)) $((y + 90))"
wait_client "dead-origin popup dismissed"
ctl "button left release"

[[ $(dump_field 'app_id=grab-dead-origin' x) == "$x0" && $(dump_field 'app_id=grab-dead-origin' y) == "$y0" ]] || {
    echo "a press on a destroyed surface moved the window"; dump_state; exit 1; }

kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true
input_health_probe
echo "OK: a press whose surface died grants neither a move nor a popup grab"
