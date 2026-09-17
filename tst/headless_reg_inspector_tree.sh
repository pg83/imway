#!/usr/bin/env bash
# The inspector lists the toplevels on screen, and each of them opens into a
# tree of what the compositor knows about it. With nothing mapped the list is
# empty and none of that ever runs.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_shm"

start_client
wait_mapped
wait_rect 'app_id=imway.client-shm'

ctl "key 125 press"; ctl "key 88 press"; ctl "key 88 release"; ctl "key 125 release" # Super+F12
await_imgui inspector || { echo "the inspector did not open"; dump_state; exit 1; }

wx=$(dump_field '^imgui name=inspector ' x)
wy=$(dump_field '^imgui name=inspector ' y)
ww=$(dump_field '^imgui name=inspector ' w)
wh=$(dump_field '^imgui name=inspector ' h)

screenshot "$XDG_RUNTIME_DIR/closed.ppm"

# The toplevel row is the first thing under the frame graph and the three
# lines of counters, and exactly where depends on the font. Walk down the
# left edge until a click opens something, which only a tree node does.
expanded() {
    screenshot "$XDG_RUNTIME_DIR/open.ppm" &&
        [[ "$(region_diff "$XDG_RUNTIME_DIR/closed.ppm" "$XDG_RUNTIME_DIR/open.ppm" \
            $((wx + 4)) $((wy + 100)) $((wx + ww - 4)) $((wy + wh - 4)))" -gt 200 ]]
}

opened=0

for y in $(seq $((wy + 110)) 6 $((wy + 200))); do
    click_at $((wx + 24)) "$y"
    expanded && { opened=1; break; }
done

(( opened )) || {
    echo "no click opened the toplevel's tree"
    dump_state
    exit 1
}

expect_alive "compositor died opening the inspector's tree"
echo "OK: the inspector lists a toplevel and opens its tree"
