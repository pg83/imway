#!/usr/bin/env bash
# xdg-system-bell rings light the whole screen white over a black window;
# switching appearance.visual_bell off puts the flash out at once, and once
# the rings stop the last flash fades back to the window's own black.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# the longest, strongest flash: the window reads near white while lit
ctl "set appearance.visual_bell_seconds 1"
ctl "set appearance.visual_bell_strength 1"
await 100 in_log "control: set appearance.visual_bell_strength" || { echo "the bell settings were not taken"; exit 1; }

start_client
wait_client "ringing"
wait_mapped
wait_rect 'app_id=bellflash'
wait_placed 'app_id=bellflash' || { echo "the window never settled"; exit 1; }

shot="$XDG_RUNTIME_DIR/bell.ppm"
await_mean "$shot" 'app_id=bellflash' '$r -gt 120 && $g -gt 120 && $b -gt 120' >/dev/null \
    || { echo "the ringing bell never lit the screen"; exit 1; }

ctl "set appearance.visual_bell 0"
await_mean "$shot" 'app_id=bellflash' '$r -lt 20 && $g -lt 20 && $b -lt 20' >/dev/null \
    || { echo "the switched-off bell kept flashing"; exit 1; }

ctl "set appearance.visual_bell 1"
await_mean "$shot" 'app_id=bellflash' '$r -gt 120 && $g -gt 120 && $b -gt 120' >/dev/null \
    || { echo "the bell switched back on did not flash again"; exit 1; }

touch "$XDG_RUNTIME_DIR/stop-go"
wait_client "stopped"
await_mean "$shot" 'app_id=bellflash' '$r -lt 20 && $g -lt 20 && $b -lt 20' >/dev/null \
    || { echo "the last flash never faded"; exit 1; }

touch "$XDG_RUNTIME_DIR/done-go"
wait_client "done"
expect_client_ok "the client failed"
expect_alive
echo "OK: the bell flashes while ringing, goes dark when switched off and fades after the last ring"
