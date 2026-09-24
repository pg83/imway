#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_PRIME=1 IMWAY_SETTINGS=display.lock_before_dpms=false
# imway-args: --dpms 1
# Idle power management and a VT comeback on the dumb-buffer path: waking
# the display and coming back from a VT switch both remodeset on the last
# dumb buffer shown, not on a scanout image there is none of. A screenshot
# taken while switched away composes without touching the display.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "dumb-buffer path (no zero-copy scanout)" || { echo "not on the dumb-buffer path"; cat "$IMWAY_LOG"; exit 1; }

await 100 in_log "display off (idle)" || { echo "display never went idle"; cat "$IMWAY_LOG"; exit 1; }

ctl "motion 100 100"
await 100 in_log "display back on" || { echo "input did not wake the display"; cat "$IMWAY_LOG"; exit 1; }

ctl "session 0"
await 50 in_log "session disabled (vt switch away)" || { echo "session did not disable"; exit 1; }
# a screenshot while away still composes, and puts nothing on the display
# (a flip already on its way when the session went can still land first)
flips_still() {
    local a
    a=$(dump_field '^kms' flips)
    sleep 0.3
    [[ "$(dump_field '^kms' flips)" == "$a" ]]
}
await 30 flips_still || { echo "the display kept flipping while switched away"; exit 1; }
away_flips=$(dump_field '^kms' flips)
screenshot "$XDG_RUNTIME_DIR/away.ppm" || { echo "no screenshot while switched away"; exit 1; }
[[ "$(dump_field '^kms' flips)" == "$away_flips" ]] || { echo "a screenshot while switched away flipped the display"; exit 1; }
ctl "session 1"
await 50 in_log "session enabled, remodeset" || { echo "no remodeset on comeback"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the comeback"; exit 1; }

expect_alive "compositor died cycling power on dumb buffers"
echo "OK: dumb-buffer outputs wake and come back on their last frame"
