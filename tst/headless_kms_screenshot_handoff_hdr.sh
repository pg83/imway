#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHILD_LOG=./viewer.log
# imway-args: --device auto --hdr 300
# The screenshot handoff on an HDR (BT.2020 + PQ) KMS session: the handed-off
# scanout is a 10-bit buffer, which the viewer imports, reads back at full
# precision and saves, as PNG mapped to SDR and as JPEG XL keeping the PQ
# code values, both matching the output's own capture.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/hdr_shot_check.sh"

in_log "HDR output: BT.2020 + PQ" || { echo "no HDR boot"; cat "$IMWAY_LOG"; exit 1; }
in_log "scanout swapchain" || { echo "no zero-copy swapchain, nothing to hand off"; cat "$IMWAY_LOG"; exit 1; }

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name handoff"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 100 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

shot() { # <file> <exits before>
    local file=$1 before=$2
    done_saving() {
        [[ -s "$file" && "$(grep -c "exited with status 0" "$IMWAY_LOG" || true)" -gt "$before" ]]
    }
    ctl "key 99 press"; ctl "key 99 release" # Print
    await 200 done_saving || {
        echo "the handoff save produced no $file"
        cat "$IMWAY_LOG" "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null
        exit 1
    }
}

screenshot "$XDG_RUNTIME_DIR/before.ppm"
shot "$shots/handoff.png" 0
in_log "screenshot handoff of the scanout buffer" || { echo "the capture was read back instead of handed off"; cat "$IMWAY_LOG"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/after.ppm"
hdr_png_check "$shots/handoff.png" "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" 4

ctl "set applications.screenshot_format 0" # jxl
ctl "set applications.screenshot_lossless true"
shot "$shots/handoff.jxl" 1
"$IMWAY_TESTS_BIN/client_jxl_dump" "$shots/handoff.jxl" "$XDG_RUNTIME_DIR/jxl.ppm" >/dev/null || {
    echo "the handed-off JPEG XL does not decode"
    exit 1
}
screenshot "$XDG_RUNTIME_DIR/after2.ppm"
hdr_jxl_check "$XDG_RUNTIME_DIR/jxl.ppm" "$XDG_RUNTIME_DIR/after.ppm" "$XDG_RUNTIME_DIR/after2.ppm" 1

expect_alive "compositor died handing off an HDR scanout"
echo "OK: an HDR scanout handed to the viewer saves as SDR PNG and PQ JPEG XL"
