#!/usr/bin/env bash
# The frame's other resize handles: the top border grows the window upward
# with its bottom edge pinned, the bottom-left grip grows it left and down
# with the right edge and the top pinned, and a client that asks for a
# top-left resize grows it toward the hand from its left and top edges. The
# client size must track the window size in every case.
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

# the client committed a new size and the frame caught up with it
settled_since() { # <client_w> <client_h>
    local g
    read -r -a g <<<"$(geometry)"
    [[ ${#g[@]} -eq 6 ]] && (( g[4] != $1 || g[5] != $2 )) && (( g[2] - g[4] == dw && g[3] - g[5] == dh ))
}

# the client asks for its resize on the first press it sees itself; the
# frame's handles are the compositor's and never reach it
start_client client-resize-top-left
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
read -r x y w h cw ch <<<"$(geometry)"

# top border, 40px up
drag $((x + w / 2)) "$y" 0 -40 27 # nsResize
await 100 settled_since "$cw" "$ch" || { echo "the top border drag did not resize: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "top: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nh >= h + 30 )) || { echo "the top drag did not grow the height ($h -> $nh)"; exit 1; }
(( ny + nh >= y + h - 2 && ny + nh <= y + h + 2 )) || { echo "the bottom edge moved on a top drag"; exit 1; }
(( nx == x && nw == w )) || { echo "a vertical drag changed x or width"; exit 1; }
x=$nx; y=$ny; w=$nw; h=$nh; cw=$ncw; ch=$nch

# bottom-left grip, 40px left and 30px down
drag $((x + 3)) $((y + h - 3)) -40 30 28 # neswResize
await 100 settled_since "$cw" "$ch" || { echo "the bottom-left grip did not resize: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "bottom-left: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nw >= w + 30 && nh >= h + 20 )) || { echo "the grip did not grow the window (${w}x${h} -> ${nw}x${nh})"; exit 1; }
(( nx + nw >= x + w - 2 && nx + nw <= x + w + 2 )) || { echo "the right edge moved on a left grip drag"; exit 1; }
(( ny == y )) || { echo "the top moved on a bottom grip drag ($y -> $ny)"; exit 1; }
x=$nx; y=$ny; w=$nw; h=$nh; cw=$ncw; ch=$nch

# the client's own press: it names the top-left corner, and the drag after
# it is the compositor's to follow
for _ in $(seq 1 25); do
    ix=$(dump_field 'app_id=resize' imgx); iy=$(dump_field 'app_id=resize' imgy)
    ctl "motion $((ix + 40)) $((iy + 40))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "motion $((ix + 41)) $((iy + 40))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "button left press"
    sleep 0.4
    grep -q "resize asked" "$CLIENT_LOG" && break
    ctl "button left release"
    sleep 0.4
done
wait_client "resize asked"
for d in 10 20 30 40; do
    ctl "motion $((ix + 41 - d)) $((iy + 40 - d))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
done
ctl "button left release"
await 100 settled_since "$cw" "$ch" || { echo "the client-driven top-left resize did not land: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "client top-left: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nw > w && nh > h )) || { echo "the top-left resize did not grow the window"; exit 1; }
(( nx + nw >= x + w - 2 && nx + nw <= x + w + 2 )) || { echo "the right edge moved on a top-left resize"; exit 1; }
(( ny + nh >= y + h - 2 && ny + nh <= y + h + 2 )) || { echo "the bottom edge moved on a top-left resize"; exit 1; }

expect_alive "compositor died resizing from the frame's edges"
echo "OK: top border, bottom-left grip and a client top-left resize anchor the opposite edges"
