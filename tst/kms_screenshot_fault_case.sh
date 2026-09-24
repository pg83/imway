# Sourced by the headless_kms_screenshot_*_fail scenarios: the screenshot
# chord on a zero-copy KMS session whose scanout handoff breaks at the step
# the scenario's IMWAY_CHAOS=scanout=K names. The capture falls back to a
# pixel readback, the file is written anyway and the session keeps flipping
# on its own swapchain.

in_log "scanout swapchain: 2 images" || { echo "no zero-copy swapchain, nothing to hand off"; cat "$IMWAY_LOG"; exit 1; }
in_log "imway: 10-bit scanout" || { echo "the boot swapchain is not the 10-bit one the fault count assumes"; cat "$IMWAY_LOG"; exit 1; }

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name fallback"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 100 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print

await 100 in_log "imway: screenshot readback" || { echo "the capture did not fall back to a readback"; cat "$IMWAY_LOG"; exit 1; }
! in_log "screenshot handoff of the scanout buffer" || { echo "a broken handoff was still taken"; cat "$IMWAY_LOG"; exit 1; }

saved() { [[ -s "$shots/fallback.png" ]]; }
await 200 saved || { echo "the readback was not saved"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(head -c 4 "$shots/fallback.png" | od -An -tx1 | tr -d ' \n')" == 89504e47 ]] || { echo "fallback.png is not a PNG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "flips stopped after the fallback"; exit 1; }

expect_alive "compositor died on a broken screenshot handoff"
echo "OK: a broken scanout handoff falls back to a readback"
