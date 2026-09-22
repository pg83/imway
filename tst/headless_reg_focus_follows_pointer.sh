#!/usr/bin/env bash
# desktop.focus_policy "follows pointer": moving the pointer onto a window
# focuses it (and raises it, raise_on_focus being on by default).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "windows mapped"
wait_rect 'app_id=ffp-a'
wait_rect 'app_id=ffp-b'

ctl "set desktop.focus_policy 1"

ida=$(dump_field 'app_id=ffp-a ' id)
idb=$(dump_field 'app_id=ffp-b ' id)

# a point inside <app_id>'s window and outside the other one
point_in() { # <app_id> <other app_id>
    local x y w h ox oy ow oh px py
    x=$(dump_field "app_id=$1 " imgx); y=$(dump_field "app_id=$1 " imgy)
    w=$(dump_field "app_id=$1 " client_w); h=$(dump_field "app_id=$1 " client_h)
    ox=$(dump_field "app_id=$2 " imgx); oy=$(dump_field "app_id=$2 " imgy)
    ow=$(dump_field "app_id=$2 " client_w); oh=$(dump_field "app_id=$2 " client_h)
    for px in $((x + 10)) $((x + w - 10)); do
        for py in $((y + 10)) $((y + h - 10)); do
            if (( px < ox || px >= ox + ow || py < oy || py >= oy + oh )); then
                echo "$px $py"
                return 0
            fi
        done
    done
    return 1
}

focused_is() { [[ "$(dump_field '^focus ' id)" == "$1" ]]; }

# Window positions are per-frame renderer truth and settle over the first
# frames, and the pointer target is picked from a rendered frame after the
# motion: re-read the rects and re-aim every round until the focus moves.
aim() { # <app_id> <other app_id> <id>
    local i p x y
    for ((i = 0; i < 30; i++)); do
        if p=$(point_in "$1" "$2"); then
            read -r x y <<<"$p"
            ctl "motion $x $y"
            sleep 0.2
            ctl "motion $((x + 1)) $y"
            sleep 0.2
            focused_is "$3" && return 0
        fi
        sleep 0.3
    done
    echo "the pointer on $1 did not focus window $3"
    dump_state
    exit 1
}

aim ffp-a ffp-b "$ida"
aim ffp-b ffp-a "$idb"
aim ffp-a ffp-b "$ida"

expect_alive "compositor died with focus following the pointer"
echo "OK: focus followed the pointer"
