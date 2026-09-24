#!/usr/bin/env bash
# A fullscreen window's move request off a valid press is dropped: the
# pointer drags on, and the window stays fullscreen where it was.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "fullscreen move ready"
fullscreen() { [[ "$(dump_field 'app_id=fullscreen-move' fullscreen)" == 1 ]]; }
await 100 fullscreen || { echo "the window never went fullscreen"; dump_state; exit 1; }
wait_placed 'app_id=fullscreen-move' || { echo "the window never settled"; exit 1; }
x0=$(dump_field 'app_id=fullscreen-move' x); y0=$(dump_field 'app_id=fullscreen-move' y)

# on the window's buffer (its own size, not the output's); pointer focus
# is worked out from a rendered frame
px=$(($(dump_field 'app_id=fullscreen-move' imgx) + 100)); py=$(($(dump_field 'app_id=fullscreen-move' imgy) + 100))
ctl "motion $px $py"; screenshot "$XDG_RUNTIME_DIR/_aim.ppm"
ctl "motion $((px + 1)) $py"; screenshot "$XDG_RUNTIME_DIR/_aim.ppm"
ctl "button left press"
wait_client "fullscreen move requested"
for step in 1 2 3 4 5; do
    ctl "motion $((px + 1 + step * 30)) $((py + step * 20))"
    screenshot "$XDG_RUNTIME_DIR/_drag.ppm"
done
ctl "button left release"

[[ "$(dump_field 'app_id=fullscreen-move' x)" == "$x0" && "$(dump_field 'app_id=fullscreen-move' y)" == "$y0" ]] || {
    echo "the fullscreen window moved: $x0,$y0 -> $(dump_field 'app_id=fullscreen-move' x),$(dump_field 'app_id=fullscreen-move' y)"
    exit 1
}
fullscreen || { echo "the move request took the window out of fullscreen"; exit 1; }

kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true
input_health_probe
echo "OK: a fullscreen window's move request is dropped"
