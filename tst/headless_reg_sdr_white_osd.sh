#!/usr/bin/env bash
# The SDR white of an HDR output shows its OSD whoever changed it, not only
# the brightness keys: here the control verb steps it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

osd_up() { dump_state | grep -q '^imgui name=##osd '; }
hdr_is() { [[ "$(dump_field '^hdr ' metadata)" == "$1" ]]; }

ctl "set display.osd_seconds 5"
ctl "set display.hdr_enabled true"
await 100 hdr_is 1 || { echo "the output did not switch to HDR"; dump_state; exit 1; }
! osd_up || { echo "an OSD was up before the SDR white changed"; dump_state; exit 1; }

ctl "sdr-white 300"
await 50 osd_up || { echo "the SDR white changed without an OSD"; dump_state; exit 1; }

expect_alive "compositor died showing the SDR white OSD"
echo "OK: an SDR white change from anywhere shows its OSD"
