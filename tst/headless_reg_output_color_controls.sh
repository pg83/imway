#!/usr/bin/env bash
# The output's color controls do only what a display would: on an
# SDR output an SDR-white change is meaningless and a night-light
# temperature at or above daylight is neutral, so neither moves a pixel,
# and a display setting that leaves the color state as it was re-renders
# nothing different. With HDR enabled, SDR white follows the command,
# except for zero and for the value it already has. A mode given on the
# command line is honoured with its refresh, and one that does not parse
# refuses to start.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shot() { # <name>
    screenshot "$XDG_RUNTIME_DIR/$1.ppm"
}
changed() { # <a> <b>
    region_diff "$XDG_RUNTIME_DIR/$1.ppm" "$XDG_RUNTIME_DIR/$2.ppm" 0 0 1280 800
}

# a steady blue-grey window gives every comparison below the same bright
# content, whatever the host's panel shows
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_tablet"
start_client
wait_client "tool ready"
wait_rect 'app_id=tablet-test'

# the rect is in the dump before a frame shows the window: the baseline is
# whatever two screenshots apart agree on
settled() { shot base; sleep 0.3; shot again; (( $(changed base again) == 0 )); }
await 50 settled || { echo "the screen never settled with the window up"; exit 1; }
ctl "sdr-white 250"
ctl "night 7000"
ctl "set display.sdr_nits 200"
await 20 in_log "control: set display.sdr_nits" || { echo "settings are not reachable"; exit 1; }
shot same
[[ "$(changed base same)" -eq 0 ]] || { echo "a no-op color control changed pixels ($(changed base same))"; exit 1; }
[[ "$(dump_field '^hdr' metadata)" == 0 ]] || { echo "an SDR output reports HDR metadata"; dump_state; exit 1; }

# the measure itself: a real night-light temperature moves the window's
# blue well past region_diff's threshold, all 400x300 of it
ctl "night 3000"
shot warm
[[ "$(changed base warm)" -gt 100000 ]] || { echo "night light 3000K changed only $(changed base warm) pixels"; exit 1; }
ctl "night 0"

cll() { dump_field '^hdr' max_cll; }
cll_is() { [[ "$(cll)" == "$1" ]]; }

ctl "set display.hdr_enabled true"
await 50 cll_is 200 || { echo "HDR at 200 nit SDR white reports max_cll $(cll)"; dump_state; exit 1; }
ctl "sdr-white 0"
ctl "sdr-white 200"
ctl "key 2 press"; ctl "key 2 release"
sleep 0.3
cll_is 200 || { echo "a zero or unchanged SDR white moved max_cll to $(cll)"; dump_state; exit 1; }
ctl "sdr-white 250"
await 50 cll_is 250 || { echo "SDR white 250 did not reach the metadata ($(cll))"; dump_state; exit 1; }

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

rc=0
out=$(IMWAY_FAKE_KMS_MODE=800x600@75 timeout 60 "$imway_bin" --device auto --socket imway-mode --frames 3 --mode 800x600@75 2>&1) || rc=$?
[[ "$rc" -eq 0 ]] || { echo "an 800x600@75 run exited $rc: $out"; exit 1; }
grep -q "output 800x600@75" <<<"$out" || { echo "the mode was not honoured: $out"; exit 1; }

# a mode without a size on either side of the x, or with a zero one, is
# no mode at all
for mode in sideways x600 800x 0x600 800x0; do
    rc=0
    out=$(timeout 60 "$imway_bin" --device auto --socket imway-mode --frames 3 --mode "$mode" 2>&1) || rc=$?
    [[ "$rc" -eq 1 ]] || { echo "the unparseable mode $mode exited $rc: $out"; exit 1; }
done

expect_alive "compositor died on the output's color controls"
echo "OK: the output's color controls change only what they should"
