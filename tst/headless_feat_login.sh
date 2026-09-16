#!/usr/bin/env bash
# --login starts the session locked: the lockscreen is up before the first
# frame, client input stays withheld until the password unlocks it, and the
# desktop underneath is what the unlock reveals.
# imway-args: --login
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "imway: login: lockscreen opened after 0 frames" ||
    { echo "lockscreen was not opened before the first frame"; cat "$IMWAY_LOG"; exit 1; }

# the lockscreen scenario's live client: KEY_F8 proves whether keyboard
# input reaches it
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_lockscreen"
start_client
wait_client "lockscreen ready"
wait_client "phase 1"
screenshot "$XDG_RUNTIME_DIR/locked.ppm"

ctl "key 66 press"; ctl "key 66 release"  # KEY_F8
sleep 0.4
kill -0 "$CLIENT_PID" || { echo "input escaped through the login lockscreen"; exit 1; }

for _ in 1 2 3; do
    ctl "key 45 press"; ctl "key 45 release" # KEY_X
    sleep 0.2
done
sleep 0.5 # let ImGui's trickle queue consume every x before Enter
ctl "key 28 press"; ctl "key 28 release" # Enter
await 100 in_log "lockscreen closed" || { echo "xxx did not close the login lockscreen"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/unlocked.ppm"

# the dialog occupied the middle of the output while locked
diff=$(region_diff "$XDG_RUNTIME_DIR/locked.ppm" "$XDG_RUNTIME_DIR/unlocked.ppm" 360 220 920 560)
[[ "$diff" -gt 10000 ]] || { echo "login lockscreen was not on screen ($diff)"; exit 1; }

ctl "key 66 press"; ctl "key 66 release"
expect_client_ok "unlock did not restore the keyboard route"
expect_alive "login lockscreen teardown killed compositor"
echo "OK: --login opens the lockscreen before the first frame and unlocks with xxx"
