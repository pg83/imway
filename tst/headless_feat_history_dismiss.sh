#!/usr/bin/env bash
# The notification history is a transient panel: opened from the launcher it
# lists what was posted, and a click anywhere else lets it go.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "notify history-app 0 0 kept"
await 50 in_log "control: notification" || { echo "the notification was not taken"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type notifications"
ctl "key 103 press"; ctl "key 103 release" # Up: select the action
ctl "key 28 press"; ctl "key 28 release"   # Enter
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }
await_imgui '##history' || { echo "the launcher did not open the history"; dump_state; exit 1; }
[[ "$(dump_field '^notifications ' history)" == 1 ]] || { echo "the history does not hold the posted notification"; exit 1; }

# the empty desktop, away from the panel
click_at 300 600
await_no_imgui '##history' || { echo "a click elsewhere did not close the history"; dump_state; exit 1; }

expect_alive "compositor died around the notification history"
echo "OK: the history opens from the launcher and closes on a click elsewhere"
