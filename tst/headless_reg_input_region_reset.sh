#!/usr/bin/env bash
# An empty input region lets the pointer pass over a window without
# entering it; set_input_region(NULL) gives the whole surface back. The
# opaque region is set and reset to none on the way.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

# aim at the window's middle as laid out now, with a composed frame after
# each motion: the hover follows the pointer one frame behind
hover() {
    local x y
    x=$(dump_field 'app_id=misc-input ' imgx); y=$(dump_field 'app_id=misc-input ' imgy)
    [[ -n "$x" && -n "$y" ]] || return 1
    ctl "motion $((x + 100)) $((y + 75))"; screenshot "$XDG_RUNTIME_DIR/_h.ppm"
    ctl "motion $((x + 101)) $((y + 75))"; screenshot "$XDG_RUNTIME_DIR/_h.ppm"
}
entered() { hover; grep -q "input region ok" "$CLIENT_LOG"; }

start_client input-region
wait_client "step 1"
wait_rect 'app_id=misc-input '
# with the empty region nothing may enter, however often the pointer passes
hover; hover
next
wait_client "input region reset"
ctl "motion 5 400"; screenshot "$XDG_RUNTIME_DIR/_h.ppm"
await 60 entered || { echo "the pointer never entered after the region reset"; cat "$CLIENT_LOG"; exit 1; }
echo "OK: an empty input region passes the pointer, a null one takes it back"
