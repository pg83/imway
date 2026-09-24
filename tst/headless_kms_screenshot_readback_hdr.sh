#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=scanout=10
# imway-args: --hdr 300
# An HDR screenshot with no handoff: the replacement scanout cannot be built
# (the first call after the ten of the boot swapchain fails), the capture
# reads the frame back, and the viewer saves the file it is given as a PNG
# mapped to SDR, matching the output's own capture.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/hdr_shot_check.sh"

in_log "HDR output: BT.2020 + PQ" || { echo "no HDR boot"; cat "$IMWAY_LOG"; exit 1; }
in_log "scanout swapchain: 2 images" || { echo "no zero-copy swapchain, nothing to hand off"; cat "$IMWAY_LOG"; exit 1; }
in_log "imway: 10-bit scanout" || { echo "the boot swapchain is not the 10-bit one the fault count assumes"; cat "$IMWAY_LOG"; exit 1; }

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name readback"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 100 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/before.ppm"
ctl "key 99 press"; ctl "key 99 release" # Print

await 100 in_log "imway: screenshot readback" || { echo "the capture did not fall back to a readback"; cat "$IMWAY_LOG"; exit 1; }
! in_log "screenshot handoff of the scanout buffer" || { echo "a broken handoff was still taken"; cat "$IMWAY_LOG"; exit 1; }

saved() { [[ -s "$shots/readback.png" ]] && in_log "exited with status 0"; }
await 200 saved || { echo "the readback was not saved"; cat "$IMWAY_LOG"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/after.ppm"
hdr_png_check "$shots/readback.png" "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" 4

expect_alive "compositor died reading back an HDR screenshot"
echo "OK: an HDR screenshot read back instead of handed off saves as SDR PNG"
