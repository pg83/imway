#!/usr/bin/env bash
# A top-left resize the client answers late: the drag ends before the
# client has committed the size it was configured with. When the buffer
# comes, the window still grows toward where the hand was, its right and
# bottom edges where they were; the compensation lasts until the client has
# answered, not just until the first frame without a new size.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_interactive_resize"

geometry() { # -> x y w h client_w client_h
    local line
    line=$(dump_state | grep 'app_id=resize')
    for f in x y w h client_w client_h; do
        awk -v f="$f" '{ for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == f) { print kv[2]; exit } }' <<<"$line"
    done | xargs
}
steady() {
    local before
    before=$(geometry)
    ctl "frame"
    [[ "$(geometry)" == "$before" ]]
}

start_client client-resize-top-left
wait_client "resize client mapped"
wait_rect 'app_id=resize'
await 50 steady || { echo "the window never held still: $(geometry)"; exit 1; }

# away from the work area's edges, by the title bar
read -r x y w h cw ch <<<"$(geometry)"
ctl "motion $((x + 60)) $((y + 10))"
compose_frame
ctl "motion $((x + 61)) $((y + 10))"
compose_frame
ctl "button left press"
compose_frame
for s in 1 2 3 4 5; do
    ctl "motion $((x + 61 + 40 * s)) $((y + 10 + 30 * s))"
    compose_frame
done
ctl "button left release"
compose_frame
moved() { [[ "$(dump_field 'app_id=resize' x)" -ge $((x + 150)) ]]; }
await 100 moved || { echo "the title bar drag did not move the window: $(geometry)"; exit 1; }
await 50 steady || { echo "the moved window never held still: $(geometry)"; exit 1; }
read -r x y w h cw ch <<<"$(geometry)"
echo "start: $x,$y ${w}x${h} client ${cw}x${ch}"

# the client names its top-left corner on the press; its answers wait
touch "$XDG_RUNTIME_DIR/hold"
for i in $(seq 1 25); do
    ix=$(dump_field 'app_id=resize' imgx); iy=$(dump_field 'app_id=resize' imgy)
    ctl "motion $((ix + 40)) $((iy + 40))"
    compose_frame
    ctl "motion $((ix + 41)) $((iy + 40))"
    compose_frame
    ctl "button left press"
    compose_frame
    grep -q "resize asked 1" "$CLIENT_LOG" && break
    ctl "button left release"
    compose_frame
done
wait_client "resize asked 1"
for s in 1 2 3 4; do
    ctl "motion $((ix + 41 - 10 * s)) $((iy + 40 - 10 * s))"
    compose_frame
done
wait_client "configure held"
ctl "button left release"
for _ in 1 2 3 4 5; do
    compose_frame
done
[[ "$(geometry)" == "$x $y $w $h $cw $ch" ]] || { echo "the window changed before the client answered: $(geometry)"; exit 1; }

# the answer comes after the drag is over
rm "$XDG_RUNTIME_DIR/hold"
grown() {
    local g
    read -r -a g <<<"$(geometry)"
    [[ ${#g[@]} -eq 6 ]] && (( g[4] > cw ))
}
await 100 grown || { echo "the late answer never landed: $(geometry)"; exit 1; }
await 50 steady || { echo "the resized window never held still: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "late answer: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nx + nw >= x + w - 1 && nx + nw <= x + w + 1 )) || { echo "the right edge moved: $((x + w)) -> $((nx + nw))"; exit 1; }
(( ny + nh >= y + h - 1 && ny + nh <= y + h + 1 )) || { echo "the bottom edge moved: $((y + h)) -> $((ny + nh))"; exit 1; }

expect_alive "compositor died on a late resize answer"
echo "OK: a late answer to a top-left resize keeps the far edges in place"
