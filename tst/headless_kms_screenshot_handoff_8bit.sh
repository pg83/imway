#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_10BIT=1 IMWAY_CHILD_LOG=./viewer.log
# The screenshot handoff from an 8-bit (XRGB8888) scanout, on a plane
# without the 10-bit formats: the viewer imports the handed-off buffer,
# reads it back as 8-bit pixels and saves a PNG equal to the output's own
# capture.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/hdr_shot_check.sh"

in_log "scanout swapchain" || { echo "no zero-copy swapchain, nothing to hand off"; cat "$IMWAY_LOG"; exit 1; }
! in_log "imway: 10-bit scanout" || { echo "the scanout is 10-bit after all"; cat "$IMWAY_LOG"; exit 1; }

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name handoff"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 100 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/before.ppm"
ctl "key 99 press"; ctl "key 99 release" # Print
saved() {
    [[ -s "$shots/handoff.png" ]] && in_log "exited with status 0"
}
viewer_failed() {
    [[ -s "$XDG_RUNTIME_DIR/viewer.log" ]] && grep -Eq "vulkan lacks VK_(KHR_external_memory_fd|EXT_external_memory_dma_buf|EXT_image_drm_format_modifier)" "$XDG_RUNTIME_DIR/viewer.log"
}
for _ in $(seq 1 200); do
    saved && break
    viewer_failed && { echo "SKIP: this device has no dma-buf image import"; exit 127; }
    sleep 0.1
done
saved || { echo "the handoff save produced no PNG"; cat "$IMWAY_LOG" "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null; exit 1; }
in_log "screenshot handoff of the scanout buffer" || { echo "the capture was read back instead of handed off"; cat "$IMWAY_LOG"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/after.ppm"

sdr_png_check "$shots/handoff.png" "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" 1

expect_alive "compositor died handing off an 8-bit scanout"
echo "OK: an 8-bit scanout handed to the viewer saves as the output's frame"
