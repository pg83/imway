#!/usr/bin/env bash
# The notification store without a bus: posts land on screen and in history,
# a bounded history drops the oldest, a replacing post reuses its slot,
# do-not-disturb keeps posts off screen both explicitly and on a schedule,
# and a critical post follows the critical setting.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

counts() { # <active> <history>
    [[ "$(dump_field '^notifications ' active)" == "$1" && "$(dump_field '^notifications ' history)" == "$2" ]]
}
active_is() {
    [[ "$(dump_field '^notifications ' active)" == "$1" ]]
}
history_is() {
    [[ "$(dump_field '^notifications ' history)" == "$1" ]]
}

ctl "set notifications.history 3"
ctl "set notifications.timeout 30"
await 20 in_log "control: set notifications.timeout" || { echo "settings are not reachable"; exit 1; }

# three posts: all on screen, all kept
for i in 1 2 3; do
    ctl "notify test 0 0 post-$i"
done
await 50 counts 3 3 || { echo "posts did not reach the store: $(dump_state | grep '^notifications')"; exit 1; }

# a toast overlay is on screen now
screenshot "$XDG_RUNTIME_DIR/toasts.ppm"

# the fourth post trims the oldest off-screen one; every one is on screen,
# so nothing can be dropped yet and the history grows past its cap
ctl "notify test 0 0 post-4"
await 50 active_is 4 || { echo "the fourth post did not show"; exit 1; }
await 50 history_is 4 || { echo "on-screen toasts must not be trimmed"; exit 1; }

# dismiss them all through the do-not-disturb switch: everything leaves the
# screen and the cap then applies
ctl "set notifications.dnd true"
await 50 active_is 0 || { echo "do-not-disturb did not clear the screen"; exit 1; }
ctl "notify test 0 0 post-5"
await 50 counts 0 3 || { echo "a bounded history did not trim: $(dump_state | grep '^notifications')"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/quiet.ppm"
[[ "$(region_diff "$XDG_RUNTIME_DIR/toasts.ppm" "$XDG_RUNTIME_DIR/quiet.ppm" 900 20 1280 400)" -gt 200 ]] || {
    echo "the toasts are still drawn under do-not-disturb"; exit 1; }

# a critical post is allowed through while the setting permits it
ctl "notify test 0 1 critical-post"
await 50 active_is 0 || { echo "a critical post ignored do-not-disturb"; exit 1; }
ctl "set notifications.critical false"
ctl "notify test 0 1 critical-denied"
await 50 active_is 0 || { echo "the critical setting changed the screen"; exit 1; }
ctl "set notifications.critical true"

# a schedule that covers the whole day keeps the screen quiet
ctl "set notifications.dnd false"
ctl "set notifications.dnd_scheduled true"
ctl "set notifications.dnd_start 0"
ctl "set notifications.dnd_end 1439"
ctl "notify test 0 0 scheduled-quiet"
await 50 active_is 0 || { echo "the all-day schedule did not silence the post"; exit 1; }

# a window a few minutes ahead does not
now=$(date +%H:%M)
minutes=$((10#${now%:*} * 60 + 10#${now#*:}))
ctl "set notifications.dnd_start $(((minutes + 5) % 1440))"
ctl "set notifications.dnd_end $(((minutes + 6) % 1440))"
ctl "notify test 0 0 outside-the-window"
await 50 active_is 1 || { echo "a post outside the schedule stayed off screen"; exit 1; }

# a replacing post reuses the slot instead of adding one
id=$(grep -o "control: notification [0-9]*" "$IMWAY_LOG" | tail -1 | awk '{print $3}')
[[ -n "$id" ]] || { echo "no notification id in the log"; exit 1; }
before=$(dump_field '^notifications ' history)
ctl "notify test $id 0 replaced"
sleep 0.5
[[ "$(dump_field '^notifications ' history)" == "$before" ]] || { echo "a replacing post added a slot"; exit 1; }
await 50 active_is 1 || { echo "the replaced post left the screen"; exit 1; }

expect_alive "compositor died driving the notification store"
echo "OK: posts, a bounded history, replacement, do-not-disturb and its schedule"
