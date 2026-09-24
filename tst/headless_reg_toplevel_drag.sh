#!/usr/bin/env bash
# xdg-toplevel-drag: the attached toplevel tracks the cursor during a
# pointer drag started from another window, and is never the drag's drop
# target, not even where it is the only window under the cursor; unmapped
# mid-drag, it is let go.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
wait_mapped 'app_id=drag-origin'
wait_mapped 'app_id=drag-torn'
sleep 0.3

wait_rect 'app_id=drag-origin'
wait_placed 'app_id=drag-origin' || { echo "the origin window never settled"; dump_state; exit 1; }
wait_placed 'app_id=drag-torn' || { echo "the torn window never settled"; dump_state; exit 1; }
ox=$(dump_field 'app_id=drag-origin' imgx)
oy=$(dump_field 'app_id=drag-origin' imgy)
# press in the origin where the torn window (and its chrome) is not, so the
# pointer grab origin is the origin surface: the torn window's placement
# depends on the host, try the origin's corners against its settled rect
txl=$(dump_field 'app_id=drag-torn' x); tyt=$(dump_field 'app_id=drag-torn' y)
txr=$((txl + $(dump_field 'app_id=drag-torn' w))); tyb=$((tyt + $(dump_field 'app_id=drag-torn' h)))
px=""
for cand in "30 270" "370 270" "370 30" "30 30"; do
    read -r cx cy <<<"$cand"
    cx=$((ox + cx)); cy=$((oy + cy))
    if (( cx < txl - 8 || cx > txr + 8 || cy < tyt - 8 || cy > tyb + 8 )); then
        px=$cx; py=$cy
        break
    fi
done
[[ -n "$px" ]] || { echo "the torn window covers every corner of the origin"; dump_state; exit 1; }

# press inside the origin so the client gets a button serial for start_drag;
# the pointer focus follows a composed frame and the ui must have let the
# pointer go (no chrome or popup under it), so aim until the dump says so
client_owns_pointer() {
    ctl "motion $px $py"; screenshot "$XDG_RUNTIME_DIR/_press.ppm"
    ctl "motion $((px + 1)) $py"; screenshot "$XDG_RUNTIME_DIR/_press.ppm"
    [[ "$(dump_state | sed -n 's/^captured kb=[0-9]* ptr=//p')" == 0 ]]
}
await 20 client_owns_pointer || { echo "the ui kept the pointer over the origin"; dump_state; exit 1; }
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

# off the origin the window under the cursor is the dragged one alone: a
# frame puts it there, and the next motion must not make it the drop target
oxr=$((ox + 400)); oyb=$((oy + 300))
tx=""
for cand in "100 650" "1100 650" "1100 100" "100 100"; do
    read -r cx cy <<<"$cand"
    if (( cx < ox - 30 || cx > oxr + 30 || cy < oy - 20 || cy > oyb + 20 )); then
        tx=$cx; ty=$cy
        break
    fi
done
[[ -n "$tx" ]] || { echo "the origin covers every corner of the screen"; dump_state; exit 1; }
ctl "motion $tx $ty"
screenshot "$XDG_RUNTIME_DIR/_off.ppm"
followed() {
    local dx dy
    dx=$(( $(dump_field 'app_id=drag-torn' x) - (tx - 20) )); dy=$(( $(dump_field 'app_id=drag-torn' y) - (ty - 10) ))
    (( ${dx#-} <= 4 && ${dy#-} <= 4 ))
}
await 50 followed || { echo "the torn window did not follow the cursor off the origin"; dump_state; exit 1; }
ctl "motion $((tx + 1)) $ty"
screenshot "$XDG_RUNTIME_DIR/_off.ppm"

# unmapped mid-drag, the window is let go: mapped again, the cursor moving
# on no longer carries it
ctl "key 57 press"
ctl "key 57 release"
wait_client "torn remapped"
remapped() { [[ "$(dump_field 'app_id=drag-torn' mapped)" == 1 ]]; }
await 50 remapped || { echo "the torn window did not map again"; dump_state; exit 1; }
# the client read every drag event before the key that remapped it
grep -q "drag entered origin" "$CLIENT_LOG" || { echo "the drag never entered the origin it started from"; cat "$CLIENT_LOG"; exit 1; }
! grep -q "drag entered torn" "$CLIENT_LOG" || { echo "the dragged window became its own drop target"; cat "$CLIENT_LOG"; exit 1; }
tx=1000
ty=150
ctl "motion $tx $ty"
screenshot "$XDG_RUNTIME_DIR/_moved.ppm"
ctl "motion $((tx + 1)) $ty"
screenshot "$XDG_RUNTIME_DIR/_moved.ppm"
wx=$(dump_field 'app_id=drag-torn' x)
wy=$(dump_field 'app_id=drag-torn' y)
dx=$(( wx - (tx + 1 - 20) )); dx=${dx#-}
dy=$(( wy - (ty - 10) )); dy=${dy#-}
(( dx > 50 || dy > 50 )) || {
    echo "the remapped window still follows the cursor: at ${wx},${wy}"
    exit 1
}

ctl "button left release"
expect_alive "compositor died unmapping the dragged window"
echo "OK: xdg-toplevel-drag moved the attached window with the cursor, and let it go on unmap"
