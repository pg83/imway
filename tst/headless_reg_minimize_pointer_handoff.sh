#!/usr/bin/env bash
# The pointer rests on the top one of three stacked windows. The top one
# minimizes itself, then the middle one unmaps: each time, with no motion at
# all, the client sees the pointer leave the hidden window and enter the one
# now under it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "windows mapped"
wait_rect 'app_id=handoff-bottom'
wait_rect 'app_id=handoff-middle'
wait_rect 'app_id=handoff-top'

last_is() { [[ "$(grep "pointer on" "$CLIENT_LOG" | tail -1)" == "pointer on $1" ]]; }
field_is() { [[ "$(dump_field "app_id=handoff-$1" "$2")" == "$3" ]]; }

# a point inside all three windows, re-read each try: the rects of the later
# windows sit at or below and right of the earlier ones' origins
aim_top() {
    local i x y a
    for i in $(seq 20); do
        x=0; y=0
        for a in bottom middle top; do
            (( $(dump_field "app_id=handoff-$a" imgx) > x )) && x=$(dump_field "app_id=handoff-$a" imgx)
            (( $(dump_field "app_id=handoff-$a" imgy) > y )) && y=$(dump_field "app_id=handoff-$a" imgy)
        done
        ctl "motion $((x + 40)) $((y + 40))"
        screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
        ctl "motion $((x + 41)) $((y + 40))"
        await 10 last_is top && return 0
    done
    echo "the pointer never entered the top window"
    dump_state
    cat "$CLIENT_LOG"
    exit 1
}
aim_top

kill -USR1 "$CLIENT_PID"
wait_client "minimize requested"
await 100 field_is top minimized 1 || { echo "the top window did not minimize"; dump_state; exit 1; }
await 100 last_is middle || { echo "the pointer did not move from the minimized window to the one under it"; dump_state; cat "$CLIENT_LOG"; exit 1; }

kill -USR2 "$CLIENT_PID"
wait_client "unmap requested"
await 100 last_is bottom || { echo "the pointer did not move from the unmapped window to the one under it"; cat "$CLIENT_LOG"; exit 1; }

expect_alive "compositor died handing the pointer on"
echo "OK: minimize and unmap hand a resting pointer to the window beneath"
