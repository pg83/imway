#!/usr/bin/env bash
# imway-args: --dpms 1
# A flip event that the idle blanking drains itself after the event loop
# has already seen the drm fd readable: the loop's own read of the fd then
# finds nothing and must not wait for an event that is not coming. Each
# round holds a frame's flip, then releases it while the compositor is busy
# with screenshots across the moment the display blanks, so the event and
# the idle timer are ready at the same wakeup; input wakes the display
# again for the next round.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
offs() { grep -c "display off (idle)" "$IMWAY_LOG" || true; }

for i in $(seq 1 8); do
    n=$(offs)
    ctl "kms-hold-flips 1"
    ctl "motion $((100 + i)) 100"
    sleep 0.85
    ctl "kms-hold-flips 0"
    for _ in 1 2 3; do
        ctl "screenshot $XDG_RUNTIME_DIR/busy.ppm"
    done
    blanked() { [[ "$(offs)" -gt "$n" ]]; }
    await 200 blanked || { echo "round $i: the display did not blank"; cat "$IMWAY_LOG"; exit 1; }
    [[ -n "$(flips)" ]] || { echo "round $i: the compositor stopped answering"; exit 1; }
done

ons=$(grep -c "display back on" "$IMWAY_LOG" || true)
ctl "motion 300 300"
woke() { [[ "$(grep -c "display back on" "$IMWAY_LOG" || true)" -gt "$ons" ]]; }
await 50 woke || { echo "input did not wake the display"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on a drained flip event"
echo "OK: a flip event drained by the idle blanking does not stall the loop"
