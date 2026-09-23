#!/usr/bin/env bash
# Pointer constraints across leaving and re-entering the surface: a oneshot
# lock unlocks on leave and stays spent, a confinement whose region misses
# the surface never confines, one limited by the input region confines,
# relative motion finds no relative pointer of the focused client (and
# another client's hears nothing), and the active confinement destroyed
# lets go.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "phase 1 ready"

# the window rect is per-frame truth and pointer focus needs a rendered
# frame after the motion: re-read, re-aim, until the client reports <marker>
aim_in() { # <marker>
    local i x y
    for ((i = 0; i < 30; i++)); do
        x=$(dump_field 'app_id=constraint-cycle' imgx)
        y=$(dump_field 'app_id=constraint-cycle' imgy)
        if [[ -n "$x" && -n "$y" ]]; then
            ctl "motion $((x + 40)) $((y + 40))"
            sleep 0.2
            ctl "motion $((x + 41)) $((y + 40))"
        fi
        grep -q "^$1\$" "$CLIENT_LOG" && return 0
        sleep 0.3
    done
    echo "never reached: $1"
    cat "$CLIENT_LOG"
    exit 1
}

# over the dock, where no client surface is
aim_out() { # <marker>
    local i
    for ((i = 0; i < 30; i++)); do
        ctl "motion 5 400"
        sleep 0.2
        ctl "motion 5 401"
        grep -q "^$1\$" "$CLIENT_LOG" && return 0
        sleep 0.3
    done
    echo "never reached: $1"
    cat "$CLIENT_LOG"
    exit 1
}

aim_in "locked 1"
aim_out "unlocked 1"
aim_in "phase 1 done"

aim_out "phase 2 out"
aim_in "phase 2 done"

aim_out "phase 3 away"
wait_client "phase 3 ready"
aim_in "confined 1"

for _ in 1 2 3; do
    ctl "relmotion 4 3"
done
# the control FIFO is served in order: once a dump comes back, the
# relative motions have been handled
dump_state >/dev/null
touch go-destroy
wait_client "constraint cycle done"

expect_alive "compositor died cycling pointer constraints"
echo "OK: constraints followed the pointer out and back in"
