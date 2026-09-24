#!/usr/bin/env bash
# A wl_surface with an xdg_surface but no role yet, taken as the cursor,
# has the cursor role: the xdg_surface's get_toplevel and get_popup are then
# refused with already_constructed.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

gone() {
    [[ -z "$(dump_field 'title=cursor-role' id)" ]]
}

# the window maps where no pointer rests: the client takes the pointer,
# sets its cursor and exits the moment it enters, so it enters on the aim
park() {
    ctl "motion 1270 790"
    compose_frame
}

for mode in toplevel popup; do
    park
    start_client "$mode"
    wait_client "cursor-role ready"
    point_at_color 255 0 0 || { echo "the $mode run's window was not found"; exit 1; }
    screenshot "$XDG_RUNTIME_DIR/_hover.ppm" # a frame computes hover
    read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
    ctl "motion $((x + 1)) $y"
    expect_client_ok "the $mode role on a cursor surface was not refused"
    await 50 gone || { echo "the $mode run's window stayed"; dump_state; exit 1; }
done

expect_alive "compositor died refusing a role on a cursor surface"
echo "OK: a cursor surface's xdg_surface takes no toplevel or popup role"
