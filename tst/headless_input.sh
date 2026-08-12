#!/usr/bin/env bash
# Typing into foot via the control channel creates a file.
# Chain: FIFO → seat → wl_keyboard → foot → pty → shell → touch.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

command -v foot >/dev/null || { echo "SKIP: foot not found"; exit 127; }

MARKER="$XDG_RUNTIME_DIR/m2ok"

foot >"$XDG_RUNTIME_DIR/foot.log" 2>&1 &

await 100 in_log "mapped" || {
    echo "foot did not map"; cat "$XDG_RUNTIME_DIR/foot.log"; exit 1; }
sleep 1 # let the shell inside foot finish starting up

ctl "type touch $MARKER"
sleep 0.3
ctl "key 28 press"   # Enter
ctl "key 28 release"

await 100 test -e "$MARKER" || {
    echo "FAIL: typed command did not run (no $MARKER)"
    tail -5 "$IMWAY_LOG" "$XDG_RUNTIME_DIR/foot.log"
    exit 1
}
echo "OK: keyboard input reached the shell inside foot"
