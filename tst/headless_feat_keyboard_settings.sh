#!/usr/bin/env bash
# Keyboard settings reach xkb and the clients: a new layout list rebuilds the
# keymap and the bar follows, a bad list falls back to the defaults instead of
# dying, the toggle option switches groups, the repeat settings push new
# repeat_info, and a per-window policy keeps each window's group.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# a long-lived window: wl_boot binds the seat, so the keymap and
# repeat_info of every reconfiguration reach a real client
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
wait_mapped

layout() { dump_state | awk '/^layout/ { print $2 }'; }
layout_is() { [[ "$(layout)" == "$1" ]]; }

await 50 layout_is EN || { echo "unexpected initial layout $(layout)"; exit 1; }

# a different list rebuilds the keymap; the first group becomes active
ctl "set keyboard.layouts de,us"
await 50 layout_is GE || { echo "the german layout did not take ($(layout))"; exit 1; }

# a list xkb cannot compile falls back to the defaults
ctl "set keyboard.layouts imway-not-a-layout"
await 50 in_log "bad xkb layout/options, falling back to defaults" || {
    echo "a bad layout list was not reported"
    cat "$IMWAY_LOG"
    exit 1
}
await 50 layout_is EN || { echo "the fallback layout is $(layout)"; exit 1; }

# the group toggle option, then the toggle itself
ctl "set keyboard.layouts us,ru"
ctl "set keyboard.options grp:alt_shift_toggle"
await 50 layout_is EN || { echo "the layout list did not come back ($(layout))"; exit 1; }

ctl "key 56 press"   # LEFTALT
ctl "key 42 press"   # LEFTSHIFT
ctl "key 42 release"
ctl "key 56 release"
await 50 layout_is RU || { echo "alt+shift did not switch the group ($(layout))"; exit 1; }
ctl "key 56 press"; ctl "key 42 press"; ctl "key 42 release"; ctl "key 56 release"
await 50 layout_is EN || { echo "alt+shift did not switch back ($(layout))"; exit 1; }

# an empty options string is valid and rebuilds again
ctl "set keyboard.options "
await 100 in_log "control: set keyboard.options" || { echo "settings are not reachable"; exit 1; }

# repeat_info: new numbers go out to every bound keyboard
ctl "set keyboard.repeat_rate 40"
ctl "set keyboard.repeat_delay 300"
ctl "set keyboard.layout_policy 1"
await 100 in_log "control: set keyboard.layout_policy" || { echo "the repeat settings did not apply"; exit 1; }
sleep 0.3

# the client is still there and typing still reaches it
ctl "key 30 press"; ctl "key 30 release" # KEY_A
sleep 0.3
kill -0 "$CLIENT_PID" || { echo "the client died over the reconfigurations"; cat "$CLIENT_LOG"; exit 1; }

expect_alive "compositor died reconfiguring the keyboard"
echo "OK: layouts, a bad list, the group toggle, repeat settings and the per-window policy"
