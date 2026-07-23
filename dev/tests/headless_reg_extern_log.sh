#!/usr/bin/env bash
# External library messages route through Composer::log: a garbage xkb
# layout makes xkbcommon complain, and its lines must arrive tagged through
# our log (the tee) instead of leaking to stderr in the library's own format.
# imway-args: --xkb-layout imway-no-such-layout
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 in_log "^xkb: " || { echo "no tagged xkbcommon lines in the log"; exit 1; }
in_log "falling back to defaults" || { echo "xkb fallback line missing"; exit 1; }

if grep -q "xkbcommon: ERROR" "$IMWAY_LOG"; then
    echo "xkbcommon still writes to stderr in its own format"; exit 1
fi

echo "OK: external library messages arrive through the log"
