#!/usr/bin/env bash
# private-session-bus
# Notifications end to end: the dbus server identifies itself, a toast
# renders top-right, CloseNotification answers with the by-request reason,
# the history panel lists what was posted and clear empties it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# toasts, calendar and history all anchor at the top-right corner
X0=930; Y0=28; X1=1272; Y1=360

screenshot "$XDG_RUNTIME_DIR/base.ppm"

start_client
wait_client "server imway"
wait_client "caps ok"
wait_client "posted one"

toast_up() {
    screenshot "$XDG_RUNTIME_DIR/toast.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/toast.ppm" $X0 $Y0 $X1 $Y1)" -gt 500 ]]
}

await 50 toast_up || { echo "toast did not render"; exit 1; }

wait_client "closed two reason 3"

# the history panel via the launcher action
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release"
sleep 0.2
ctl "type notifications"
sleep 0.3
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
sleep 0.3

screenshot "$XDG_RUNTIME_DIR/pre_panel.ppm" || true

panel_open() {
    screenshot "$XDG_RUNTIME_DIR/panel.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/toast.ppm" "$XDG_RUNTIME_DIR/panel.ppm" $X0 $Y0 $X1 $Y1)" -gt 800 ]]
}

await 50 panel_open || { echo "history panel did not open"; exit 1; }

# "clear" sits at the right end of the panel's header row
click_at 1240 40

cleared() {
    screenshot "$XDG_RUNTIME_DIR/cleared.ppm" || return 1
    [[ "$(region_diff "$XDG_RUNTIME_DIR/panel.ppm" "$XDG_RUNTIME_DIR/cleared.ppm" $X0 60 $X1 $Y1)" -gt 300 ]]
}

await 50 cleared || { echo "clear did not empty the history"; exit 1; }

expect_alive "compositor died during the notifications flow"
echo "OK: notifications post, close with reason, list and clear"
