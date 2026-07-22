#!/usr/bin/env bash
# tablet-v2: proximity/down/motion/pressure/up frames reach the surface
# under the virtual pen with surface-local coordinates.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "tool ready"
wait_mapped 'app_id=tablet-test'
sleep 0.3

x=$(dump_field 'app_id=tablet-test' imgx)
y=$(dump_field 'app_id=tablet-test' imgy)
cx=$((x + 50))
cy=$((y + 60))

# hover picking follows the pen one frame behind: the first proximity
# event lands before the frame that moves the hover, the follow-up motion
# (as any real pen stream has) is what enters the surface
ctl "tablet proximity_in $cx $cy"
sleep 0.3
ctl "tablet motion $cx $cy"
wait_client "prox_in"
wait_client "motion 50 60"

ctl "tablet down $cx $cy 0.5"
wait_client "tablet: down"
wait_client "pressure 32767"

ctl "tablet motion $((cx + 20)) $((cy + 10)) 0.75"
wait_client "motion 70 70"
wait_client "pressure 49151"

ctl "tablet up $((cx + 20)) $((cy + 10))"
wait_client "tablet: up"

ctl "tablet proximity_out $((cx + 20)) $((cy + 10))"
wait_client "prox_out"

echo "OK: tablet-v2 delivered proximity/down/motion/pressure/up with local coords"
