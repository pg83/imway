#!/usr/bin/env bash
# The frame's other resize handles: the top border grows the window upward
# and the bottom border downward, the bottom-left grip left and down, each
# with the opposite edges pinned and the client size tracking the window.
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

# press at (x,y) after two frames of hover, walk the pointer by (dx,dy) in
# steps, then release. With <cursor>, the hover must have drawn that cursor
# kind first (the dump's CursorKind ordinal): the handle is what ImGui
# thinks is under the pointer.
drag() { # <x> <y> <dx> <dy> [cursor]
    ctl "motion $1 $2"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "motion $1 $2"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    if [[ -n "${5:-}" ]]; then
        want_cursor=$5
        cursor_is() { [[ "$(dump_field '^cursor ' drawn)" == "$want_cursor" ]]; }
        await 50 cursor_is || { echo "hovering $1,$2 drew cursor $(dump_field '^cursor ' drawn), not $want_cursor"; exit 1; }
    fi
    ctl "button left press"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    for s in 1 2 3 4 5; do
        ctl "motion $(($1 + $3 * s / 5)) $(($2 + $4 * s / 5))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    done
    ctl "button left release"
}

# the geometry a composed frame leaves as it was: a commit's new size is in
# the dump at once, the position anchored to it only from the next frame
steady() {
    local before
    before=$(geometry)
    ctl "frame"
    [[ "$(geometry)" == "$before" ]]
}

# the client committed a new size and the frame caught up with it
settled_since() { # <client_w> <client_h>
    local g
    read -r -a g <<<"$(geometry)"
    [[ ${#g[@]} -eq 6 ]] && (( g[4] != $1 || g[5] != $2 )) && (( g[2] - g[4] == dw && g[3] - g[5] == dh ))
}

start_client
wait_client "resize client mapped"
wait_rect 'app_id=resize'
sleep 0.5 # the SSD reconfigure settles the first frames
read -r x y w h cw ch <<<"$(geometry)"
# the frame around the client: constant across resizes
dw=$((w - cw)); dh=$((h - ch))
echo "start: $x,$y ${w}x${h} client ${cw}x${ch}"

# away from the work area's edges first, by the title bar, so no drag below
# is clamped by the dock or the top bar
drag $((x + 60)) $((y + 10)) 200 150
moved() {
    [[ "$(dump_field 'app_id=resize' x)" -ge $((x + 150)) && "$(dump_field 'app_id=resize' y)" -ge $((y + 100)) ]]
}
await 100 moved || { echo "the title bar drag did not move the window: $(geometry)"; exit 1; }
await 50 steady || { echo "the moved window never held still: $(geometry)"; exit 1; }
read -r x y w h cw ch <<<"$(geometry)"

# top border, 40px up
drag $((x + w / 2)) "$y" 0 -40 27 # nsResize
await 100 settled_since "$cw" "$ch" || { echo "the top border drag did not resize: $(geometry)"; exit 1; }
await 50 steady || { echo "the resized window never held still: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "top: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nh >= h + 30 )) || { echo "the top drag did not grow the height ($h -> $nh)"; exit 1; }
(( ny + nh >= y + h - 2 && ny + nh <= y + h + 2 )) || { echo "the bottom edge moved on a top drag"; exit 1; }
(( nx == x && nw == w )) || { echo "a vertical drag changed x or width"; exit 1; }
x=$nx; y=$ny; w=$nw; h=$nh; cw=$ncw; ch=$nch

# bottom-left grip, 40px left and 30px down
drag $((x + 3)) $((y + h - 3)) -40 30 28 # neswResize
await 100 settled_since "$cw" "$ch" || { echo "the bottom-left grip did not resize: $(geometry)"; exit 1; }
await 50 steady || { echo "the resized window never held still: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "bottom-left: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nw >= w + 30 && nh >= h + 20 )) || { echo "the grip did not grow the window (${w}x${h} -> ${nw}x${nh})"; exit 1; }
(( nx + nw >= x + w - 2 && nx + nw <= x + w + 2 )) || { echo "the right edge moved on a left grip drag"; exit 1; }
(( ny == y )) || { echo "the top moved on a bottom grip drag ($y -> $ny)"; exit 1; }
x=$nx; y=$ny; w=$nw; h=$nh; cw=$ncw; ch=$nch

# bottom border, 30px down: the top stays
drag $((x + w / 2)) $((y + h - 1)) 0 30 27 # nsResize
await 100 settled_since "$cw" "$ch" || { echo "the bottom border drag did not resize: $(geometry)"; exit 1; }
await 50 steady || { echo "the resized window never held still: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "bottom: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nh >= h + 20 && ny == y && nw == w )) || { echo "the bottom drag did not grow the height alone"; exit 1; }
x=$nx; y=$ny; w=$nw; h=$nh; cw=$ncw; ch=$nch

expect_alive "compositor died resizing from the frame's edges"
echo "OK: the top and bottom borders and the bottom-left grip anchor the opposite edges"
