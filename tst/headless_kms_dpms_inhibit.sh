#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# Display power management against idle inhibitors and its own settings: an
# inhibitor on a shown popup keeps the display on past the timeout, one on a
# surface without content does not; switching the timeout off wakes a dark
# display; with lock_before_dpms off the session goes dark unlocked; and a
# timeout re-armed while dark fires into nothing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

offs() { grep -c "display off (idle)" "$IMWAY_LOG" || true; }
offs_at_least() { [[ "$(offs)" -ge "$1" ]]; }
ons_at_least() { [[ "$(grep -c "display back on" "$IMWAY_LOG" || true)" -ge "$1" ]]; }

ctl "set display.lock_before_dpms false"
start_client
wait_client "inhibited"

# the timeout starts once the inhibitors are in place; three timeouts'
# worth with the popup's inhibitor alive
ctl "set display.dpms_seconds 1"
sleep 3
[[ "$(offs)" == 0 ]] || { echo "the display went off under an inhibitor"; cat "$IMWAY_LOG"; exit 1; }

touch go-release
wait_client "released"
await 100 offs_at_least 1 || { echo "the display never went off after the inhibitor died"; cat "$IMWAY_LOG"; exit 1; }

# the timeout switched off wakes the dark display
ctl "set display.dpms_seconds 0"
await 100 ons_at_least 1 || { echo "switching dpms off left the display dark"; cat "$IMWAY_LOG"; exit 1; }

ctl "set display.dpms_seconds 1"
await 100 offs_at_least 2 || { echo "the display did not go off again"; cat "$IMWAY_LOG"; exit 1; }

# re-armed while dark: the timer fires, the display is already off
ctl "set display.dpms_seconds 1.5"
sleep 2.5
[[ "$(offs)" == 2 ]] || { echo "a dark display was turned off again"; cat "$IMWAY_LOG"; exit 1; }

ctl "motion 100 100"
await 100 ons_at_least 2 || { echo "input did not wake the display"; cat "$IMWAY_LOG"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_awake.ppm"
! dump_state | grep -q '##lock-overlay' || { echo "the display went dark locked with lock_before_dpms off"; exit 1; }

expect_alive "compositor died across dpms and inhibitors"
echo "OK: dpms followed inhibitors and its settings"
