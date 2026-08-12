#!/usr/bin/env bash
# An shm buffer beyond the device texture limit is capped at commit: logged,
# no content, no client-sized allocation reaching the top-level catch.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "oversize survived"
expect_client_ok "oversized-shm client failed"

in_log "shm buffer 17000x17000 exceeds the device limit" || {
    echo "the oversize cap did not trigger"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died on an oversized shm buffer"
echo "OK: oversized shm degrades at commit, the session stays"
