#!/usr/bin/env bash
# Alt+Tab: with two toplevels, the switcher overlay appears while Alt is held.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "two toplevels mapped"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/base.ppm"

# hold Alt, tap Tab → the switcher overlay shows
ctl "key 56 press"   # KEY_LEFTALT
ctl "key 15 press"   # KEY_TAB
ctl "key 15 release"

changed=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/overlay.ppm"
    changed=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/overlay.ppm" 200 100 1080 700)
    [[ "$changed" -gt 2000 ]] && break
done
ctl "key 56 release" # commit the switch
echo "overlay changed=$changed"
[[ "$changed" -gt 2000 ]] || { echo "alt-tab overlay did not appear"; exit 1; }
echo "OK: Alt+Tab raised the window switcher overlay"
