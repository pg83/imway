#!/usr/bin/env bash
# The calendar's month arrows walk across year boundaries both ways and lay
# every month out on its own weeks: the panel is one text row per week, so
# its height tells four-, five- and six-week months apart. February 2021
# starts on a Monday and is not a leap month (four weeks); February 2016
# also starts on a Monday but has a 29th (five); January 2017 starts on a
# Sunday (six). Walking back to them from today crosses the new year going
# backwards, walking forward from 2016 crosses it going forwards. A click
# anywhere else lets the calendar go.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# the clock sits at the far right of the bar
click_at 1235 10
cal_open() { [[ -n "$(dump_field '^imgui name=##calendar ' x)" ]]; }
await 50 cal_open || { echo "the clock did not open the calendar"; dump_state; exit 1; }
cx=$(dump_field '^imgui name=##calendar ' x); cy=$(dump_field '^imgui name=##calendar ' y)

weeks() { # <year> <month 1..12>
    python3 -c 'import calendar, sys; y, m = int(sys.argv[1]), int(sys.argv[2]); first, days = calendar.monthrange(y, m); print(-(-(first + days) // 7))' "$1" "$2"
}
height() { dump_field '^imgui name=##calendar ' h; }

# today's month is where the panel opens
read -r year month < <(date '+%Y %m')
month=$((10#$month))
now_weeks=$(weeks "$year" "$month")
h_now=$(height)

frames() { dump_field '^frames ' done; }

# press the arrow <n> times. ImGui trickles the button through one edge a
# frame and the compositor keeps drawing while any is queued, so the walk is
# over once twice as many frames as clicks have gone by.
arrow() { # <x> <n>
    local i
    ctl "motion $1 $((cy + 18))"
    screenshot "$XDG_RUNTIME_DIR/_a.ppm"
    ctl "motion $(($1 + 1)) $((cy + 18))"
    screenshot "$XDG_RUNTIME_DIR/_a.ppm"
    f0=$(frames); n=$2
    for ((i = 0; i < $2; i++)); do
        ctl "button left press"; ctl "button left release"
    done
    drained() { (( $(frames) >= f0 + 2 * n + 4 )); }
    await 600 drained || { echo "the clicks never drained: $(frames) frames since $f0"; exit 1; }
}
prev_x=$((cx + 18))
next_x=$((cx + 242))

# the height a month of <weeks> must settle at, against today's
settles() { # <weeks> <what>
    local want=$1 h
    ok() {
        h=$(height)
        if ((want < now_weeks)); then
            ((h < h_now))
        elif ((want > now_weeks)); then
            ((h > h_now))
        else
            ((h == h_now))
        fi
    }
    await 20 ok || { echo "$2: a $want-week month left the panel at ${h}px (today, $now_weeks weeks: ${h_now}px)"; exit 1; }
}

back=$(((year - 2021) * 12 + month - 2))
arrow "$prev_x" "$back"
settles "$(weeks 2021 2)" "February 2021"

arrow "$prev_x" 60
settles "$(weeks 2016 2)" "February 2016"

arrow "$next_x" 11
settles "$(weeks 2017 1)" "January 2017"

# a click on the empty desktop: the calendar lets go of itself
click_at 700 500
cal_closed() { ! cal_open; }
await 50 cal_closed || { echo "the calendar stayed open after a click elsewhere"; dump_state; exit 1; }

expect_alive "compositor died walking the calendar"
echo "OK: the calendar walks months across year boundaries and lays each out on its weeks"
