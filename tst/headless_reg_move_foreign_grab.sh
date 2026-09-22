#!/usr/bin/env bash
# A client asks to move its window while the held button belongs to another
# client's window: the grab is not its to use, nothing moves.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

client="$IMWAY_TESTS_BIN/client_wl_misc"
pos() { echo "$(dump_field "app_id=$1 " x) $(dump_field "app_id=$1 " y)"; }

"$client" foreign-move >"$XDG_RUNTIME_DIR/foreign.log" 2>&1 &
await 100 grep -q ready "$XDG_RUNTIME_DIR/foreign.log" || { echo "foreign client not ready"; cat "$XDG_RUNTIME_DIR/foreign.log"; exit 1; }
wait_rect 'app_id=misc-foreign-move '
IMWAY_CLIENT="$client"
start_client stale-move
wait_client "ready"
wait_rect 'app_id=misc-stale-move '
point_at_color 255 0 0 || { echo "grab window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_m.ppm"
ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_m.ppm"
ctl "button left press"
before=$(pos misc-foreign-move)
touch "$XDG_RUNTIME_DIR/go-foreign-move"
await 100 grep -q "foreign move sent" "$XDG_RUNTIME_DIR/foreign.log" || { echo "foreign move not sent"; cat "$XDG_RUNTIME_DIR/foreign.log"; exit 1; }
ctl "motion $((x + 80)) $((y + 60))"; screenshot "$XDG_RUNTIME_DIR/_m.ppm"
ctl "motion $((x + 90)) $((y + 70))"; screenshot "$XDG_RUNTIME_DIR/_m.ppm"
after=$(pos misc-foreign-move)
ctl "button left release"
[[ "$before" == "$after" ]] || { echo "another client's grab moved the window: $before -> $after"; exit 1; }
expect_alive
echo "OK: a grab held by another client cannot move this one"
