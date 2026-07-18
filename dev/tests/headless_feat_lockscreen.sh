#!/usr/bin/env bash
# Super+L opens a live blurred lockscreen; desktop shortcuts and client input
# stay below its input sink, invalid passwords stay locked, and xxx unlocks.
# imway-env: IMWAY_FORCE_CURSOR=1
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "lockscreen ready"
wait_client "phase 1"
screenshot "$XDG_RUNTIME_DIR/base.ppm"

ctl "key 125 press"  # KEY_LEFTMETA
ctl "key 38 press"   # KEY_L
ctl "key 38 release"
ctl "key 125 release"

locked=0
for _ in $(seq 1 30); do
    sleep 0.15
    screenshot "$XDG_RUNTIME_DIR/locked.ppm"
    locked=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/locked.ppm" 360 220 920 560)
    [[ "$locked" -gt 10000 ]] && break
done
[[ "$locked" -gt 10000 ]] || { echo "lockscreen did not appear ($locked)"; exit 1; }

# A client key and two compositor shortcuts are all below LockSink.
ctl "key 66 press"; ctl "key 66 release"  # KEY_F8
ctl "key 56 press"; ctl "key 15 press"; ctl "key 15 release"; ctl "key 56 release"  # Alt-Tab
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # launcher
sleep 0.4
kill -0 "$CLIENT_PID" || { echo "input escaped through lockscreen"; exit 1; }

# The background is not a snapshot: poll screenshots until one of the
# client's alternating color frames reaches the filtered output.
screenshot "$XDG_RUNTIME_DIR/live-before.ppm"
live=0
for _ in $(seq 1 30); do
    sleep 0.15
    screenshot "$XDG_RUNTIME_DIR/live.ppm"
    live=$(region_diff "$XDG_RUNTIME_DIR/live-before.ppm" "$XDG_RUNTIME_DIR/live.ppm" 0 30 1280 780)
    [[ "$live" -gt 1000 ]] && break
done
[[ "$live" -gt 1000 ]] || { echo "locked background is not live ($live)"; exit 1; }

ctl "type nope"
sleep 0.4 # let ImGui's trickle queue consume all text before Enter
ctl "key 28 press"; ctl "key 28 release" # Enter
await 50 in_log "lockscreen rejected" || { echo "invalid password was not checked"; exit 1; }
await 50 in_log "lockscreen refocused" || { echo "password field did not refocus"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/rejected.ppm"
kill -0 "$CLIENT_PID" || { echo "invalid password unlocked"; exit 1; }

for _ in 1 2 3; do
    ctl "key 45 press"; ctl "key 45 release" # KEY_X
    sleep 0.2
done
ctl "key 28 press"; ctl "key 28 release"
await 50 in_log "lockscreen closed" || { echo "xxx did not close lockscreen"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/after-xxx.ppm"
ctl "key 66 press"; ctl "key 66 release"

expect_client_ok "xxx did not unlock or keyboard route was not restored"
expect_alive "lockscreen teardown killed compositor"
echo "OK: live lockscreen filters background, captures input, and unlocks with xxx"
