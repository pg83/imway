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

# wl-copy has to reach the compositor and be handed the selection before
# anyone can read it back, and how long that takes is the build's business,
# not a number this scenario can pick
pasted() { # <expected> [wl-paste args...]
    local want="$1" got
    shift

    got="$(timeout 5 wl-paste "$@" 2>/dev/null)" || return 1

    [[ "$got" == "$want" ]]
}

echo -n "clipboard payload" | wl-copy --foreground &
pids+=("$!")
await 100 pasted "clipboard payload" || {
    echo "the clipboard never carried the payload"
    exit 1
}

echo -n "primary payload" | wl-copy --foreground --primary &
pids+=("$!")
await 100 pasted "primary payload" --primary || {
    echo "the primary selection never carried the payload"
    exit 1
}

echo "OK: clipboard and primary selection round-trip"
