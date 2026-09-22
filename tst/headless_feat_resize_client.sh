#!/usr/bin/env bash
# The client's own resizes: one that asks for its top-left corner grows the
# window toward the hand with the far edges pinned, one that asks for the
# top edge alone ignores the hand's sideways part, and a corner dragged far
# past the opposite edges stops at a one-pixel client. The client size must
# track the window size in every case.
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

# The client's own presses: it names the top-left corner, then the top edge
# alone, then the corner again, and the drag after each is the
# compositor's to follow. Press inside the client until it has asked <n>
# times, walk the pointer by (dx,dy), let go.
client_drag() { # <n> <dx> <dy>
    local i
    for i in $(seq 1 25); do
        ix=$(dump_field 'app_id=resize' imgx); iy=$(dump_field 'app_id=resize' imgy)
        ctl "motion $((ix + 40)) $((iy + 40))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
        ctl "motion $((ix + 41)) $((iy + 40))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
        ctl "button left press"
        sleep 0.4
        grep -q "resize asked $1" "$CLIENT_LOG" && break
        ctl "button left release"
        sleep 0.4
    done
    wait_client "resize asked $1"
    for i in 1 2 3 4; do
        ctl "motion $((ix + 41 + $2 * i / 4)) $((iy + 40 + $3 * i / 4))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    done
    ctl "button left release"
}

client_drag 1 -40 -40
await 100 settled_since "$cw" "$ch" || { echo "the client-driven top-left resize did not land: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "client top-left: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nw > w && nh > h )) || { echo "the top-left resize did not grow the window"; exit 1; }
(( nx + nw >= x + w - 2 && nx + nw <= x + w + 2 )) || { echo "the right edge moved on a top-left resize"; exit 1; }
(( ny + nh >= y + h - 2 && ny + nh <= y + h + 2 )) || { echo "the bottom edge moved on a top-left resize"; exit 1; }
x=$nx; y=$ny; w=$nw; h=$nh; cw=$ncw; ch=$nch

# the top edge alone: a sideways hand does not change the width
client_drag 2 30 -30
await 100 settled_since "$cw" "$ch" || { echo "the client-driven top resize did not land: $(geometry)"; exit 1; }
read -r nx ny nw nh ncw nch <<<"$(geometry)"
echo "client top: $nx,$ny ${nw}x${nh} client ${ncw}x${nch}"
(( nh > h && ncw == cw )) || { echo "the top resize did not grow the height alone"; exit 1; }
x=$nx; y=$ny; w=$nw; h=$nh; cw=$ncw; ch=$nch

# the corner again, dragged far past the opposite edges: the window shrinks
# to the smallest client the frame allows, one pixel each way
client_drag 3 $((w + 100)) $((h + 100))
tiny() { [[ "$(dump_field 'app_id=resize' client_w)" == 1 && "$(dump_field 'app_id=resize' client_h)" == 1 ]]; }
await 100 tiny || { echo "dragging past the far edges did not stop at a one-pixel client: $(geometry)"; exit 1; }

expect_alive "compositor died following the client's own resizes"
echo "OK: the client's top-left and top resizes anchor the far edges and stop at one pixel"
