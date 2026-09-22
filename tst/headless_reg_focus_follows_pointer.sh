#!/usr/bin/env bash
# desktop.focus_policy "follows pointer": moving the pointer onto a window
# focuses it, with raise_on_focus on and off. With it off the window under
# the pointer stays below the other one, and its focus must hold there
# rather than snap back to the window on top; a click on the top window's
# title bar then takes it back.
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

# the focus stays put for a while, a dump every tenth of a second
holds() { # <id>
    local i
    for ((i = 0; i < 10; i++)); do
        focused_is "$1" || { echo "the focus on $1 snapped to $(dump_field '^focus ' id)"; dump_state; exit 1; }
        sleep 0.1
    done
}

aim ffp-a ffp-b "$ida"
aim ffp-b ffp-a "$idb"
aim ffp-a ffp-b "$ida"

# raise off: ffp-a stays on top while ffp-b, under it, takes the focus
ctl "set desktop.raise_on_focus 0"
aim ffp-b ffp-a "$idb"
holds "$idb"
aim ffp-a ffp-b "$ida"
holds "$ida"

# ImGui still focuses ffp-a, the window on top, while the scene focus is on
# ffp-b below it; a click on ffp-a's title bar must take the focus back
# although ImGui sees no focus change. Clicks focus from here on.
aim ffp-b ffp-a "$idb"
holds "$idb"
ctl "set desktop.focus_policy 0"
click_at $(($(dump_field 'app_id=ffp-a ' x) + 20)) $(($(dump_field 'app_id=ffp-a ' y) + 8))
await 50 focused_is "$ida" || { echo "a title bar click did not take the focus back"; dump_state; exit 1; }
holds "$ida"

expect_alive "compositor died with focus following the pointer"
echo "OK: focus followed the pointer"
