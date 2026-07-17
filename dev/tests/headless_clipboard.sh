#!/usr/bin/env bash
# Clipboard + primary selection via wl-copy/wl-paste.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

command -v wl-copy >/dev/null || { echo "SKIP: wl-clipboard not found"; exit 127; }

pids=()
cleanup() {
    for pid in "${pids[@]}"; do
        kill -KILL "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT

echo -n "clipboard payload" | wl-copy --foreground &
pids+=("$!")
sleep 0.7
GOT="$(timeout 5 wl-paste)"
[[ "$GOT" == "clipboard payload" ]] || { echo "clipboard: got '$GOT'"; exit 1; }

echo -n "primary payload" | wl-copy --foreground --primary &
pids+=("$!")
sleep 0.7
GOT="$(timeout 5 wl-paste --primary)"
[[ "$GOT" == "primary payload" ]] || { echo "primary: got '$GOT'"; exit 1; }

echo "OK: clipboard and primary selection round-trip"
