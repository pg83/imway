#!/usr/bin/env bash
# A settings file whose counts run past their tables: forty notification
# rules in a table of thirty-two, twenty input devices in a table of sixteen.
# Every reader stops at the table's end (an index past it is an assertion,
# not a read): the notifier still decides a toast, the notifications page
# lists what the table holds and offers no slot to add one, and the input
# page draws its rows.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set notifications.rule_count 40"
ctl "set input.device_count 20"
await 100 in_log "control: set input.device_count" || { echo "settings are not reachable"; exit 1; }

# every rule in the table is empty, so none names this application and its
# toast takes the default policy
ctl "notify overrun 0 0 hello"
active_is() { [[ "$(dump_field '^notifications ' active)" == "$1" ]]; }
await 50 active_is 1 || { echo "the notifier did not decide the toast past a count beyond its table"; dump_state; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "the launcher did not open"; dump_state; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }
await_imgui settings || { echo "settings did not open"; dump_state; exit 1; }

wx=$(dump_field '^imgui name=settings ' x)
wy=$(dump_field '^imgui name=settings ' y)
ww=$(dump_field '^imgui name=settings ' w)
wh=$(dump_field '^imgui name=settings ' h)

# nav entries are one text line plus item spacing apart
page() { # <index>: switch to a page and wait for its pane to change
    screenshot "$XDG_RUNTIME_DIR/before.ppm"
    click_at $((wx + 40)) $((wy + 38 + $1 * 20))
    changed() {
        screenshot "$XDG_RUNTIME_DIR/after.ppm"
        (( $(region_diff "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" \
            $((wx + 160)) $((wy + 30)) $((wx + ww - 4)) $((wy + wh - 4))) > 300 ))
    }
    await 50 changed || { echo "nav entry $1 did not switch the page"; dump_state; exit 1; }
}

page 4 # input: sixteen device rows
page 7 # notifications: thirty-two rule rows, no add button

expect_alive "compositor died reading settings counts past their tables"
echo "OK: counts past their tables are read only up to the tables"
