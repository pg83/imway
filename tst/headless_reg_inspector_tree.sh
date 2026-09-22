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

# The toplevel row is the first thing under the frame graph, the three lines
# of counters and the separator, and exactly where depends on the font. Walk
# down the left edge until a click opens something under the clicked line:
# only the tree node puts anything there. The counters above change on
# their own (the focus line with every click), so they are left out.
expanded() { # <y>
    screenshot "$XDG_RUNTIME_DIR/open.ppm" &&
        [[ "$(region_diff "$XDG_RUNTIME_DIR/closed.ppm" "$XDG_RUNTIME_DIR/open.ppm" \
            $((wx + 4)) $(($1 + 12)) $((wx + ww - 4)) $(($1 + 72)))" -gt 200 ]]
}

opened=0

for y in $(seq $((wy + 146)) 4 $((wy + 190))); do
    ctl "motion $((wx + ww / 2)) $((wy + wh - 20))"
    screenshot "$XDG_RUNTIME_DIR/_settle.ppm"
    screenshot "$XDG_RUNTIME_DIR/closed.ppm"
    click_at $((wx + 24)) "$y"
    ctl "motion $((wx + ww / 2)) $((wy + wh - 20))"
    await 10 expanded "$y" && { opened=1; break; }
done

(( opened )) || {
    echo "no click opened the toplevel's tree"
    dump_state
    exit 1
}

expect_alive "compositor died opening the inspector's tree"
echo "OK: the inspector lists a toplevel and opens its tree"
