#!/usr/bin/env bash
# start_drag on a serial that is not the button's cancels its source and
# starts nothing, with or without a source; a drag icon surface keeps its
# role, so a second drag can take it again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "mapped"
wait_rect 'app_id=dnd-error'
x=$(( $(dump_field 'app_id=dnd-error' imgx) + 100 )); y=$(( $(dump_field 'app_id=dnd-error' imgy) + 80 ))

press_drag() { # <round>
    ctl "motion $x $y"
    screenshot "$XDG_RUNTIME_DIR/_dnd.ppm"
    ctl "motion $((x + 1)) $y"
    screenshot "$XDG_RUNTIME_DIR/_dnd.ppm"
    ctl "button left press"
    wait_client "dragging $1"
    ctl "motion $((x + 5)) $((y + 2))"
    wait_client "entered $1"
    ctl "button left release"
}

press_drag 1
grep -q "wrong serial refused" "$CLIENT_LOG" || { echo "the wrong-serial drags were not refused first"; cat "$CLIENT_LOG"; exit 1; }
press_drag 2
expect_client_ok "start_drag's edges went wrong"
expect_alive "compositor died on start_drag's edges"
input_health_probe
echo "OK: wrong-serial drags start nothing and a drag icon serves a second drag"
