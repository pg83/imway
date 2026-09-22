#!/usr/bin/env bash
# An empty input region lets the pointer pass over a window without
# entering it; set_input_region(NULL) gives the whole surface back. The
# opaque region is set and reset to none on the way.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

hover() { # the hover follows the pointer one composed frame behind
    ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_h.ppm"
    ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_h.ppm"
}

start_client input-region
wait_client "step 1"
wait_rect 'app_id=misc-input '
x=$(( $(dump_field 'app_id=misc-input ' imgx) + 100 ))
y=$(( $(dump_field 'app_id=misc-input ' imgy) + 75 ))
hover
next
wait_client "step 2"
ctl "motion 5 400"; screenshot "$XDG_RUNTIME_DIR/_h.ppm"
hover
next
wait_client "input region ok"
echo "OK: an empty input region passes the pointer, a null one takes it back"
