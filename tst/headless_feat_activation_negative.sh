#!/usr/bin/env bash
# Anti-focus-stealing: an activation token requested WITHOUT an input serial
# must not move focus when spent. The thief fires only after it sees the
# victim take focus.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client victim
wait_client "victim ready"
VICTIM_PID=$CLIENT_PID

CLIENT_LOG="$XDG_RUNTIME_DIR/thief.log" IMWAY_CLIENT_LOG="$XDG_RUNTIME_DIR/thief.log" \
    start_client thief
CLIENT_LOG="$XDG_RUNTIME_DIR/thief.log"
wait_client "thief ready"
sleep 0.3

# focus the victim with a real click into a corner the thief window does not
# cover (a color-centroid click can land on the overlapping thief)
vx=$(dump_field 'app_id=victim' imgx); vy=$(dump_field 'app_id=victim' imgy)
click_at $((vx + 280)) $((vy + 30))
wait_client "stole attempt"
sleep 0.5

focus_victim=$(dump_field 'app_id=victim' focused)
thief_act=$(dump_field 'app_id=thief' activated)
echo "victim focused=$focus_victim thief activated=$thief_act"
[[ "$focus_victim" == 1 ]] || { echo "BUG: unauthorized activation stole focus"; exit 1; }
[[ "$thief_act" == 0 ]] || { echo "BUG: thief got activated"; exit 1; }

kill -0 "$VICTIM_PID" 2>/dev/null || { echo "victim died"; exit 1; }
expect_alive "compositor died on unauthorized activation"
echo "OK: a serial-less activation token does not steal focus"
