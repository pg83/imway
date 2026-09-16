#!/usr/bin/env bash
# The desktop's own cursor kinds: imgui's resize cursors over a window frame
# become the wayland shapes the dump reports. The client here only has to
# hold a window on screen; the border zone belongs to the desktop.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_shm"
start_client
wait_mapped

# the window's own rect, not the client content: the grip sits in its frame
wx=$(dump_field '^imgui name=client_shm###toplevel' x)
wy=$(dump_field '^imgui name=client_shm###toplevel' y)
ww=$(dump_field '^imgui name=client_shm###toplevel' w)
wh=$(dump_field '^imgui name=client_shm###toplevel' h)
[[ -n "$wx" && -n "$ww" ]] || { echo "no toplevel window in the dump"; dump_state; exit 1; }

shape() {
    dump_field '^cursor shape' drawn
}

# CursorKind ordinals: ewResize=26, nsResize=27, nwseResize=29
hover_is() { # <x> <y> <expected>
    local x=$1 y=$2 want=$3 i
    for ((i = 0; i < 40; i++)); do
        ctl "motion $x $y"
        sleep 0.2
        [[ "$(shape)" = "$want" ]] && return 0
    done

    echo "cursor at $x,$y is $(shape), expected $want" >&2

    return 1
}

hover_is $((wx + ww - 2)) $((wy + wh / 2)) 26 || { echo "the right border is not an east-west resize"; exit 1; }
hover_is $((wx + ww / 2)) $((wy + wh - 2)) 27 || { echo "the bottom border is not a north-south resize"; exit 1; }
hover_is $((wx + ww - 2)) $((wy + wh - 2)) 29 || { echo "the bottom right corner is not a diagonal resize"; exit 1; }

expect_alive "the cursor walk took the compositor with it"
echo "OK: the window frame reports the resize cursor shapes"
