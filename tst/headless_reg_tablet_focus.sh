#!/usr/bin/env bash
# tablet-v2 focus between a window and its subsurface and off the client:
# proximity follows the surface under the pen, a doubled tip press or a
# stray lift sends nothing, a surface the pen leaves touching sees the tip
# lift and the one it enters sees it land, and leaving proximity or every
# surface with the tip down lifts it first.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "tool ready"
wait_rect 'app_id=tablet-focus'
wait_placed 'app_id=tablet-focus' || { echo "the window never settled"; exit 1; }
x=$(dump_field 'app_id=tablet-focus' imgx)
y=$(dump_field 'app_id=tablet-focus' imgy)
main="$((x + 50)) $((y + 100))"
sub="$((x + 250)) $((y + 100))"

# hover picking follows the pen one frame behind: every move is followed by
# a composed frame and the same move again
pen() { # <phase> <x> <y> [axis ...]
    ctl "tablet $*"
    screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
}

count() { # <event>: how many the client has seen so far
    local n
    n=$(grep "^$1 " "$CLIENT_LOG" | tail -n 1 | awk '{print $NF}')
    echo "${n:-0}"
}

moved_past() { # <motion count>
    [[ "$(count motion)" -gt "$1" ]]
}

# a motion after the step: once it arrived, whatever the step sent has too
settle() { # <x> <y>
    local m
    m=$(count motion)
    pen motion "$@"
    await 50 moved_past "$m" || { echo "no motion after the step"; cat "$CLIENT_LOG"; exit 1; }
}

# the line the client printed <event> on, for ordering checks
line_of() { # <event line>
    grep -n -x "$1" "$CLIENT_LOG" | head -n 1 | cut -d: -f1
}

in_order() { # <first> <second>
    local a b
    a=$(line_of "$1")
    b=$(line_of "$2")
    [[ -n "$a" && -n "$b" && "$a" -lt "$b" ]] || { echo "\"$1\" did not come before \"$2\""; cat "$CLIENT_LOG"; exit 1; }
}

pen proximity_in $main
# the first pick waits for a frame that has the window hovered: move again
# until the client hears the tool come in
in_main() { pen motion $main; grep -q "^prox_in main 1$" "$CLIENT_LOG"; }
await 50 in_main || { echo "the pen never came into the window"; cat "$CLIENT_LOG"; exit 1; }

# a lift with the tip not down is no up
pen up $main
settle $main
[[ "$(count up)" == 0 ]] || { echo "a lift with the tip not down sent up"; cat "$CLIENT_LOG"; exit 1; }

pen down $main 0.5
wait_client "down 1"
# pressed again while down: no second down
pen down $main 0.5
settle $main
[[ "$(count down)" == 1 ]] || { echo "a second press while down sent another down"; cat "$CLIENT_LOG"; exit 1; }

# onto the subsurface with the tip down: the window it leaves sees the tip
# lift before the tool goes, the subsurface sees the tool come in touching
pen motion $sub
pen motion $sub
wait_client "prox_in sub 2"
wait_client "down 2"
in_order "up 1" "prox_out 1"
in_order "prox_out 1" "prox_in sub 2"
in_order "prox_in sub 2" "down 2"

pen up $sub
wait_client "up 2"

# out of proximity with the tip down: up first
pen down $sub 0.5
wait_client "down 3"
pen proximity_out $sub
wait_client "prox_out 2"
in_order "up 3" "prox_out 2"

# back in, tip down, and off every surface of the client: up, then out
pen proximity_in $sub
pen motion $sub
wait_client "prox_in sub 3"
pen down $sub 0.5
wait_client "down 4"
pen motion 5 400
pen motion 5 400
wait_client "prox_out 3"
in_order "up 4" "prox_out 3"

expect_alive "compositor died moving the pen between surfaces"
echo "OK: tablet focus followed the pen and lifted the tip on the way out"
