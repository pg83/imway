#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=clock-ms=4294955296
# The 32-bit millisecond clock starts 12 seconds short of its wrap round
# zero. An OSD brought up just before the wrap goes after its three
# seconds, instead of reading a deadline above the wrapped clock as time
# still to come and staying up.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

osd_up() { dump_state | grep -q '^imgui name=##osd '; }
osd_gone() { ! osd_up; }
hdr_is() { [[ "$(dump_field '^hdr ' metadata)" == "$1" ]]; }
clock() { dump_field '^clock ' ms; }
before_wrap() { (( $(clock) >= 2147483648 )); }
near_wrap() { (( $(clock) >= 4294967296 - 1500 )); }
wrapped() { (( $(clock) < 2147483648 )); }

ctl "set display.osd_seconds 3"
ctl "set display.hdr_enabled true"
await 100 hdr_is 1 || { echo "the output did not switch to HDR"; dump_state; exit 1; }
before_wrap || { echo "the clock did not start just short of its wrap ($(clock))"; exit 1; }

await 150 near_wrap || { echo "the clock never came near its wrap ($(clock))"; exit 1; }
ctl "key 225 press"; ctl "key 225 release" # KEY_BRIGHTNESSUP
await 50 osd_up || { echo "the brightness key showed no OSD"; dump_state; exit 1; }
before_wrap || { echo "the OSD came up after the wrap: the scenario started too late ($(clock))"; exit 1; }

await 100 wrapped || { echo "the clock did not wrap ($(clock))"; exit 1; }
await 60 osd_gone || { echo "the OSD brought up before the wrap stayed after it"; dump_state; exit 1; }

expect_alive "compositor died with the OSD across the clock's wrap"
echo "OK: an OSD brought up before the clock's wrap goes after it"
