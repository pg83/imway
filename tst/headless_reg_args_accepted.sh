#!/usr/bin/env bash
# imway-args: --output HDMI-A-1 --font ./no-such-font.ttf --rgb-range auto --rgb-range full
# The documented options the other scenarios never pass: an output name, a
# font that is not there (the renderer falls back to the system font) and
# the RGB range given twice, the last one winning. The compositor accepts
# them all, where an unknown option would stop it at startup with usage.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

first=$(dump_field '^frames done' done)
frames_advanced() { [[ "$(dump_field '^frames done' done)" -gt "$first" ]]; }
ctl "motion 300 300"
await 50 frames_advanced || { echo "the compositor stopped drawing with the extra options"; cat "$IMWAY_LOG"; exit 1; }
! in_log "usage" || { echo "an option was refused"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died on documented options"
echo "OK: output, font and RGB range options are accepted"
