#!/usr/bin/env bash
# imway-env: IMWAY_CHILD_LOG=./viewer.log
# A mode change probed while a screenshot is building the scanout buffer
# it will lend its viewer: switching now would rebuild the swapchain under
# it, so the switch is refused for now, and a hotplug event after the
# screenshot is done takes it. A second Print while the first screenshot
# is in flight is not taken.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "scanout swapchain" || { echo "no zero-copy swapchain, nothing to hand off"; cat "$IMWAY_LOG"; exit 1; }
in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "set applications.screenshot_directory $XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_name deferred"
ctl "set applications.screenshot_action 1" # save, no window
await 100 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

flips() { dump_field '^kms' flips; }

# a frame whose flip never completes: the screenshot cannot reach its own
# frame and stays in flight
ctl "kms-hold-flips 1"
ctl "motion 300 300"
ctl "key 99 press"; ctl "key 99 release" # Print
ctl "key 99 press"; ctl "key 99 release" # Print again, while it is busy
f0=$(flips)

ctl "kms-modes 1"
ctl "kms-connector 1"
await 100 in_log "mode switch deferred, screenshot in flight" || { echo "the mode switch did not wait for the screenshot"; cat "$IMWAY_LOG"; exit 1; }
! in_log "kms output: 1920x1080@60" || { echo "the mode switched under the screenshot"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(flips)" == "$f0" ]] || { echo "a flip completed while flips were held"; exit 1; }

ctl "kms-hold-flips 0"
await 100 in_log "screenshot handoff of the scanout buffer" || { echo "the screenshot did not go on"; cat "$IMWAY_LOG"; exit 1; }
switched() {
    ctl "motion 310 310"
    ctl "kms-connector 1"
    in_log "kms output: 1920x1080@60"
}
await 100 switched || { echo "no hotplug event after the screenshot switched the mode"; cat "$IMWAY_LOG"; exit 1; }

saved() { in_log "exited with status"; }
await 100 saved || { echo "the screenshot's viewer never finished"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(grep -c "screenshot handoff of the scanout buffer" "$IMWAY_LOG")" == 1 ]] || { echo "the Print while busy was taken"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died deferring a mode switch"
echo "OK: a mode switch waits for a screenshot in flight"
