#!/usr/bin/env bash
# xdg-toplevel-drag: the attached toplevel tracks the cursor during a
# pointer drag started from another window.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
wait_mapped 'app_id=drag-origin'
wait_mapped 'app_id=drag-torn'
sleep 0.3

ox=$(dump_field 'app_id=drag-origin' imgx)
oy=$(dump_field 'app_id=drag-origin' imgy)
# press low in the origin, clear of the torn window that cascades over its
# top-left, so the pointer grab origin is the origin surface
px=$((ox + 30))
py=$((oy + 270))

# press inside the origin so the client gets a button serial for start_drag
ctl "motion $px $py"
sleep 0.1
ctl "button left press"
sleep 0.2

# release the client into the drag phase
ctl "key 57 press"
ctl "key 57 release"
wait_client "dragging"
sleep 0.3

# move the pointer; the torn window's top-left must follow to cursor-offset
tx=700
ty=520
ctl "motion $tx $ty"
sleep 0.3

wx=$(dump_field 'app_id=drag-torn' x)
wy=$(dump_field 'app_id=drag-torn' y)

# attach offset was (20,10); allow a couple px of chrome/rounding slack
dx=$(( wx - (tx - 20) )); dx=${dx#-}
dy=$(( wy - (ty - 10) )); dy=${dy#-}
[[ "$dx" -le 4 && "$dy" -le 4 ]] || {
    echo "torn window did not follow the cursor: at ${wx},${wy}, expected ~$((tx-20)),$((ty-10))"
    exit 1
}

ctl "button left release"
echo "OK: xdg-toplevel-drag moved the attached window with the cursor"
