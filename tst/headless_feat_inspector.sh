#!/usr/bin/env bash
# Inspector: Super+F12 toggles the inspector overlay.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

screenshot "$XDG_RUNTIME_DIR/base.ppm"

ctl "key 125 press"  # Super
ctl "key 88 press"   # F12
ctl "key 88 release"
ctl "key 125 release"

changed=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/after.ppm"
    changed=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/after.ppm" 200 100 1080 700)
    [[ "$changed" -gt 2000 ]] && break
done
echo "changed=$changed"
[[ "$changed" -gt 2000 ]] || { echo "inspector overlay did not appear"; exit 1; }
echo "OK: Super+F12 toggled the inspector overlay"
