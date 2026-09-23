#!/usr/bin/env bash
# The OSD and bell durations take any number from a settings file or the
# FIFO, but are held within 0..60 seconds: a negative one shows nothing
# for no time at all (it used to wrap round to an OSD and a flash that
# stayed for weeks), a huge one lasts the minute.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

osd_up() { dump_state | grep -q '^imgui name=##osd '; }
osd_gone() { ! osd_up; }
hdr_is() { [[ "$(dump_field '^hdr ' metadata)" == "$1" ]]; }
brightness_key() { ctl "key 225 press"; ctl "key 225 release"; } # KEY_BRIGHTNESSUP

ctl "set display.hdr_enabled true"
await 100 hdr_is 1 || { echo "the output did not switch to HDR"; dump_state; exit 1; }

# a negative OSD duration: no OSD stays up
ctl "set display.osd_seconds -5"
await 20 in_log "control: set display.osd_seconds" || { echo "settings are not reachable"; exit 1; }
brightness_key
screenshot "$XDG_RUNTIME_DIR/_o.ppm"
await 20 osd_gone || { echo "a negative OSD duration kept the OSD up"; dump_state; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_o.ppm"
osd_gone || { echo "a negative OSD duration kept the OSD up"; dump_state; exit 1; }

# a huge one: held at the minute, the OSD is up a while later still
ctl "set display.osd_seconds 99999999999"
brightness_key
await 50 osd_up || { echo "a huge OSD duration showed no OSD"; dump_state; exit 1; }
sleep 1
osd_up || { echo "a huge OSD duration did not keep the OSD up"; dump_state; exit 1; }
ctl "set display.hdr_enabled false"
await 100 hdr_is 0 || { echo "the output did not leave HDR"; exit 1; }

# a negative bell duration: no flash lingers after the rings stop
ctl "set appearance.visual_bell_seconds -1"
ctl "set appearance.visual_bell_strength 1"
await 100 in_log "control: set appearance.visual_bell_strength" || { echo "the bell settings were not taken"; exit 1; }
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_system_bell_flash"
start_client
wait_client "ringing"
wait_rect 'app_id=bellflash'
wait_placed 'app_id=bellflash' || { echo "the window never settled"; exit 1; }
touch "$XDG_RUNTIME_DIR/stop-go"
wait_client "stopped"
await_mean "$XDG_RUNTIME_DIR/bell.ppm" 'app_id=bellflash' '$r -lt 20 && $g -lt 20 && $b -lt 20' >/dev/null \
    || { echo "a negative bell duration left the screen lit"; exit 1; }
touch "$XDG_RUNTIME_DIR/done-go"
expect_client_ok "the bell client failed"

expect_alive "compositor died on out-of-range durations"
echo "OK: OSD and bell durations are held within 0..60 seconds"
