#!/usr/bin/env bash
# Fullscreen round-trip: set_fullscreen sizes to the output, unset restores
# the exact pre-fullscreen client size and the window floats again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

fs_is() { # <0|1>
    [[ $(dump_field 'app_id=fsrestore' fullscreen) == "$1" ]]
}

cw_is() { # <width>
    [[ $(dump_field 'app_id=fsrestore' client_w) == "$1" ]]
}

start_client
wait_client "fs client mapped"
sleep 0.3
[[ $(dump_field 'app_id=fsrestore' client_w) == 300 ]] || { echo "bad initial size"; exit 1; }

ctl "key 33 press"; ctl "key 33 release"   # KEY_F
wait_client "fs=1"
await 30 fs_is 1 || { echo "window did not go fullscreen"; exit 1; }
await 30 cw_is 1280 || { echo "fullscreen size wrong: $(dump_field 'app_id=fsrestore' client_w)"; exit 1; }

ctl "key 22 press"; ctl "key 22 release"   # KEY_U
await 30 fs_is 0 || { echo "window did not leave fullscreen"; exit 1; }
await 30 cw_is 300 || { echo "pre-fullscreen size not restored: $(dump_field 'app_id=fsrestore' client_w)"; exit 1; }

ch=$(dump_field 'app_id=fsrestore' client_h)
(( ch == 200 )) || { echo "height not restored: $ch"; exit 1; }
echo "OK: fullscreen round-trip restored the floating 300x200"
