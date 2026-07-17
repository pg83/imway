#!/usr/bin/env bash
# Launcher: Super+F2 opens the launcher overlay; Escape closes it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

screenshot "$XDG_RUNTIME_DIR/base.ppm"

ctl "key 125 press"  # Super
ctl "key 60 press"   # F2
ctl "key 60 release"
ctl "key 125 release"

opened=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/open.ppm"
    opened=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/open.ppm" 400 180 880 460)
    [[ "$opened" -gt 2000 ]] && break
done
[[ "$opened" -gt 2000 ]] || { echo "launcher did not open ($opened)"; exit 1; }

ctl "key 1 press"    # Escape
ctl "key 1 release"

closed=99999
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/closed.ppm"
    closed=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/closed.ppm" 400 180 880 460)
    [[ "$closed" -lt 1000 ]] && break
done
echo "opened=$opened closed=$closed"
[[ "$closed" -lt 1000 ]] || { echo "launcher did not close ($closed)"; exit 1; }
echo "OK: launcher opened on Super+F2 and closed on Escape"
