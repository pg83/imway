#!/usr/bin/env bash
# Every positioner anchor and gravity, each one placed so the popup would
# leave the work area: whichever adjustment the case allows, the placed
# rectangle must end up inside the output. The client holds each popup until
# KEY_1 releases it, so the dump is read while it is still mapped.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "sweep ready 1280x800"

check() { # <case name>
    wait_client "case $1 mapped"

    local px py pw ph
    px=$(dump_field '^popup' x); py=$(dump_field '^popup' y)
    pw=$(dump_field '^popup' w); ph=$(dump_field '^popup' h)

    [[ -n "$px" && -n "$pw" ]] || { echo "$1: no popup in the dump"; dump_state; cat "$CLIENT_LOG"; exit 1; }
    echo "$1: ${pw}x${ph} at $px,$py"

    if [[ "$1" == resize ]]; then
        # larger than the output on both axes: only a resize can place it
        (( pw < 1400 && ph < 900 )) || { echo "resize did not shrink the popup"; exit 1; }
    fi

    # the adjustments whose outcome is exact: slid flush to the edge, cut to
    # the part on screen, or spanning the axis when no part is
    local want=""
    case "$1" in
        slide-left) want="0 $py 200 150" ;;
        resize-top) want="$px 0 200 50" ;;
        resize-left) want="0 $py 1280 150" ;;
        resize-above) want="$px 0 200 800" ;;
        resize-bottom) want="$px 770 200 30" ;;
        resize-right) want="1220 $py 60 150" ;;
    esac
    [[ -z "$want" || "$px $py $pw $ph" == "$want" ]] || {
        echo "$1: placed at $px $py ${pw}x${ph}, want $want"
        exit 1
    }

    (( px >= 0 && py >= 0 && px + pw <= 1280 && py + ph <= 800 )) || {
        echo "$1: the popup is off screen"
        cat "$CLIENT_LOG"
        exit 1
    }

    ctl "key 2 press"; ctl "key 2 release" # KEY_1: let the client move on
}

for name in top bottom left right top-left bottom-left top-right bottom-right \
            none flip-then-slide-x flip-then-slide-y resize slide-left too-wide \
            too-tall resize-top resize-left resize-above resize-bottom resize-right; do
    check "$name"
done

wait_client "sweep done"
expect_client_ok "the positioner sweep client failed"
expect_alive "compositor died placing constrained popups"
echo "OK: every anchor and gravity lands inside the output"
