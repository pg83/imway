#!/usr/bin/env bash
# tablet-v2: proximity/down/motion/pressure/up frames reach the surface
# under the virtual pen with surface-local coordinates.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "tool ready"
wait_mapped 'app_id=tablet-test'
sleep 0.3

wait_rect 'app_id=tablet-test'
x=$(dump_field 'app_id=tablet-test' imgx)
y=$(dump_field 'app_id=tablet-test' imgy)
cx=$((x + 50))
cy=$((y + 60))

# hover picking follows the pen one frame behind: the first proximity
# event lands before the frame that moves the hover, the follow-up motion
# (as any real pen stream has) is what enters the surface
ctl "tablet proximity_in $cx $cy"
# the hover follows the pen one frame behind, so compose one here instead
# of hoping the compositor had a reason to
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "tablet motion $cx $cy"
wait_client "prox_in"
wait_client "motion 50 60"

ctl "tablet down $cx $cy 0.5"
wait_client "tablet: down"
wait_client "pressure 32767"

ctl "tablet motion $((cx + 20)) $((cy + 10)) 0.75"
wait_client "motion 70 70"
wait_client "pressure 49151"

# every other axis the tool can carry, in one frame
ctl "tablet motion $((cx + 20)) $((cy + 10)) distance=0.25 tilt=10,-5 rotation=45 slider=0.5 wheel=15,2 button=330,press"
wait_client "distance 16383"
wait_client "tilt 10 -5"
wait_client "rotation 45"
wait_client "slider 32767"
wait_client "wheel 15 2"
wait_client "button 330 1"

ctl "tablet motion $((cx + 20)) $((cy + 10)) button=330,release"
wait_client "button 330 0"

ctl "tablet up $((cx + 20)) $((cy + 10))"
wait_client "tablet: up"

ctl "tablet proximity_out $((cx + 20)) $((cy + 10))"
wait_client "prox_out"

# and the other way out of proximity: the surface the tool is on goes away
ctl "tablet proximity_in $cx $cy"
wait_client "prox_in"
ctl "tablet down $cx $cy 0.5"
wait_client "tablet: down"

touch go-destroy
ctl "tablet motion $cx $cy"
wait_client "surface gone"
wait_client "tablet: up"
wait_client "prox_out"

echo "OK: tablet-v2 delivered proximity, the tool axes and its button with local coords"
