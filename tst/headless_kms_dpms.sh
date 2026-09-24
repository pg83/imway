#!/usr/bin/env bash
# imway-args: --dpms 1
# Idle power management on the fake KMS: a second of no input turns the
# display off (ACTIVE=0 commit), input turns it back on and flips resume.
# A wake the display refuses is logged and does not count as back on; the
# next idle cycle's wake brings it back.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

await 100 in_log "display off (idle)" || {
    echo "display never went idle"
    cat "$IMWAY_LOG"
    exit 1
}

ctl "motion 100 100"

await 100 in_log "display back on" || {
    echo "input did not wake the display"
    cat "$IMWAY_LOG"
    exit 1
}

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after wake"; exit 1; }

offs() { grep -c "display off (idle)" "$IMWAY_LOG" || true; }
ons() { grep -c "display back on" "$IMWAY_LOG" || true; }
idle_again() { [[ "$(offs)" -ge 2 ]]; }
await 100 idle_again || { echo "display never went idle again"; cat "$IMWAY_LOG"; exit 1; }

# the wake's modeset fails with the cursor plane and without it
ctl "kms-fail-commit 22 2 0"
ctl "motion 200 200"
await 100 in_log "kms atomic commit failed, errno 22 (modeset)" || { echo "the refused wake was not reported"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(ons)" == 1 ]] || { echo "a refused wake was reported as back on"; cat "$IMWAY_LOG"; exit 1; }

idle_third() { [[ "$(offs)" -ge 3 ]]; }
await 100 idle_third || { echo "display never went idle after the refused wake"; cat "$IMWAY_LOG"; exit 1; }
ctl "motion 100 100"
back_again() { [[ "$(ons)" -ge 2 ]]; }
await 100 back_again || { echo "the next wake did not bring the display back"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died across a dpms cycle"
echo "OK: idle turns the display off, input brings it back"
