#!/usr/bin/env bash
# The brightness keys on an HDR output step the SDR white and show its OSD;
# when HDR goes away while that OSD is up, the OSD goes with it instead of
# lingering over an output it no longer describes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

osd_up() { dump_state | grep -q '^imgui name=##osd '; }
osd_gone() { ! osd_up; }
hdr_is() { [[ "$(dump_field '^hdr ' metadata)" == "$1" ]]; }

ctl "set display.osd_seconds 5"
ctl "set display.hdr_enabled true"
await 100 hdr_is 1 || { echo "the output did not switch to HDR"; dump_state; exit 1; }

ctl "key 225 press"; ctl "key 225 release" # KEY_BRIGHTNESSUP
await 50 osd_up || { echo "the brightness key showed no OSD on the HDR output"; dump_state; exit 1; }

ctl "set display.hdr_enabled false"
await 100 hdr_is 0 || { echo "the output did not leave HDR"; exit 1; }
await 20 osd_gone || { echo "the HDR OSD outlived HDR"; dump_state; exit 1; }

expect_alive "compositor died with the HDR OSD up"
echo "OK: the HDR brightness OSD goes when HDR does"
