#!/usr/bin/env bash
# xdg-system-bell with appearance.visual_bell off: rings are accepted and
# flash nothing (the bell count stays at zero).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set appearance.visual_bell 0"
await 100 in_log "control: set appearance.visual_bell" || { echo "the setting was not taken"; exit 1; }
start_client
# the client prints after a roundtrip, so both rings have been handled
wait_client "rang"

count=$(dump_field 'bell' count)
[[ "${count:-x}" == 0 ]] || { echo "a switched-off bell still rang (bell count=$count)"; exit 1; }
expect_alive
echo "OK: the switched-off bell stayed dark"
