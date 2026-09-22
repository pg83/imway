#!/usr/bin/env bash
# desktop.focus_policy "follows pointer": moving the pointer onto a window
# focuses it, with raise_on_focus on and off.
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

# the windows stay put: work the points out once
pa=$(point_in ffp-a ffp-b) || { echo "no free point in ffp-a"; dump_state; exit 1; }
pb=$(point_in ffp-b ffp-a) || { echo "no free point in ffp-b"; dump_state; exit 1; }

aim() { # <point> <id>
    local i
    for ((i = 0; i < 20; i++)); do
        ctl "motion $1"
        screenshot "$XDG_RUNTIME_DIR/_ffp.ppm"
        ctl "motion $1"
        focused_is "$2" && return 0
        sleep 0.2
    done
    echo "the pointer at $1 did not focus window $2"
    dump_state
    exit 1
}

aim "$pa" "$ida"
ctl "set desktop.raise_on_focus 0"
aim "$pb" "$idb"
aim "$pa" "$ida"

expect_alive "compositor died with focus following the pointer"
echo "OK: focus followed the pointer"
