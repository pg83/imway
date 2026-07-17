#!/usr/bin/env bash
# Positioner constraint adjustment on a fullscreen parent (1280x800): a
# 200x150 popup anchored to the screen's bottom-right corner with gravity
# bottom-right. slide must clamp it to (1080,650); flip must mirror it around
# the anchor to (1070,640). Position asserted via the control dump.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

near() { # <val> <want> <tol>
    local d=$(( $1 - $2 ))
    (( d < 0 )) && d=$(( -d ))
    (( d <= $3 ))
}

check_mode() { # <mode> <want_x> <want_y>
    start_client "$1"
    wait_client "popup mapped"
    sleep 0.3

    local px py pw ph
    px=$(dump_field '^popup' x); py=$(dump_field '^popup' y)
    pw=$(dump_field '^popup' w); ph=$(dump_field '^popup' h)
    echo "$1: popup at $px,$py size=${pw}x${ph}"

    [[ -n "$px" ]] || { echo "$1: popup missing from dump"; cat "$CLIENT_LOG"; exit 1; }
    (( pw == 200 && ph == 150 )) || { echo "$1: popup size wrong"; exit 1; }
    near "$px" "$2" 2 || { echo "$1: x=$px, want $2"; cat "$CLIENT_LOG"; exit 1; }
    near "$py" "$3" 2 || { echo "$1: y=$py, want $3"; cat "$CLIENT_LOG"; exit 1; }
    (( px >= 0 && py >= 0 && px + pw <= 1280 && py + ph <= 800 )) || {
        echo "$1: popup sticks out of the screen"; exit 1; }

    kill "$CLIENT_PID" 2>/dev/null || true
    wait "$CLIENT_PID" 2>/dev/null || true
    sleep 0.3
}

# slide: clamped flush to the screen edges
check_mode slide 1080 650
# flip: mirrored to the other side of the 10x10 anchor rect
check_mode flip 1070 640

expect_alive "compositor died placing constrained popups"
echo "OK: positioner slide and flip keep the popup on screen where the spec says"
