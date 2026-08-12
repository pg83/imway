#!/usr/bin/env bash
# Dock MRU order: gaining focus stamps the window with a fresh focus_seq,
# losing focus changes nothing; the dock sorts by the stamp. Alt+Tab drives
# the focus between two windows with distinct app_ids.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "order-a mapped"
wait_client "order-b mapped"

seq_of() { # <app_id>
    dump_field "app_id=$1" focus_seq
}

leader_is() { # <app_id> <other>
    local a b
    a=$(seq_of "$1"); b=$(seq_of "$2")
    [[ "$a" =~ ^[0-9]+$ && "$b" =~ ^[0-9]+$ && "$a" -gt "$b" ]]
}

# b mapped last and took focus on appearing: its stamp is the freshest
await 50 leader_is order-b order-a || {
    echo "initial order wrong: a=$(seq_of order-a) b=$(seq_of order-b)"
    exit 1
}

alt_tab() {
    ctl "key 56 press"   # Alt
    ctl "key 15 press"   # Tab
    ctl "key 15 release"
    ctl "key 56 release" # commit on release
}

alt_tab
await 50 leader_is order-a order-b || {
    echo "focus switch did not promote: a=$(seq_of order-a) b=$(seq_of order-b)"
    exit 1
}

# the focused app_id is the first element of the top bar
screenshot "$XDG_RUNTIME_DIR/bar-a.ppm"
alt_tab
await 50 leader_is order-b order-a || {
    echo "second switch did not promote: a=$(seq_of order-a) b=$(seq_of order-b)"
    exit 1
}

screenshot "$XDG_RUNTIME_DIR/bar-b.ppm"
bar_diff=$(region_diff "$XDG_RUNTIME_DIR/bar-a.ppm" "$XDG_RUNTIME_DIR/bar-b.ppm" 58 0 300 25)
[[ "$bar_diff" -gt 5 ]] || {
    echo "bar app_id did not change with the focus ($bar_diff)"
    exit 1
}

expect_alive
echo "OK: focus stamps drive the dock MRU order, bar shows the focused app_id"
