#!/usr/bin/env bash
# The client destroys the tablet and tool it was announced, and has set a
# cursor on the tool first; a pen stream over its window afterwards must find
# nothing to send to and leave the compositor running.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

start_client tablet
wait_client "step 1"
wait_rect 'app_id=misc-tablet '
x=$(( $(dump_field 'app_id=misc-tablet ' imgx) + 50 ))
y=$(( $(dump_field 'app_id=misc-tablet ' imgy) + 50 ))
ctl "tablet proximity_in $x $y"
screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
ctl "tablet motion $x $y"
ctl "tablet down $x $y 0.5"
ctl "tablet up $x $y"
ctl "tablet proximity_out $x $y"
screenshot "$XDG_RUNTIME_DIR/_after.ppm"
expect_alive "the pen stream to a client without its tool killed the compositor"
kill -0 "$CLIENT_PID" || { echo "the client was disconnected"; cat "$CLIENT_LOG"; exit 1; }
echo "OK: a client may drop its tablet and tool"
