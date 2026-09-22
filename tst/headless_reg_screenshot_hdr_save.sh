#!/usr/bin/env bash
# imway-args: --hdr 203
# Saving from an HDR session. A PNG is an SDR image: the viewer decodes the
# captured PQ code values to nits, maps them to the SDR range with the
# display mapping and writes sRGB, SDR white (203 nits) landing on 255, so
# the PNG's neutral pixels must match that transform of the output's own PQ
# pixels, not the PQ codes themselves. A JPEG XL keeps the frame HDR.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/hdr_shot_check.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name hdr"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/before.ppm"
ctl "key 99 press"; ctl "key 99 release" # Print

saved() {
    [[ -s "$shots/hdr.png" ]] && in_log "exited with status 0"
}
await 200 saved || { echo "the HDR save produced no PNG"; cat "$IMWAY_LOG"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/after.ppm"

hdr_png_check "$shots/hdr.png" "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" 3

# JPEG XL keeps the HDR frame as it is: lossless, tagged BT.2100 PQ, so the
# decoded code values are the output's own
ctl "set applications.screenshot_name hdr-jxl"
ctl "set applications.screenshot_format 0" # jxl
ctl "set applications.screenshot_lossless true"
ctl "key 99 press"; ctl "key 99 release"

jxl_saved() {
    [[ -s "$shots/hdr-jxl.jxl" && $(grep -c "exited with status 0" "$IMWAY_LOG") -ge 2 ]]
}
await 200 jxl_saved || { echo "the HDR save produced no JPEG XL"; cat "$IMWAY_LOG"; exit 1; }
"$IMWAY_TESTS_BIN/client_jxl_dump" "$shots/hdr-jxl.jxl" "$XDG_RUNTIME_DIR/jxl.ppm" >/dev/null || {
    echo "the HDR JPEG XL does not decode"
    exit 1
}

hdr_jxl_check "$XDG_RUNTIME_DIR/jxl.ppm" "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" 1

expect_alive "compositor died saving an HDR screenshot"
echo "OK: an HDR session saves an sRGB PNG mapped from its PQ pixels and a PQ JPEG XL"
