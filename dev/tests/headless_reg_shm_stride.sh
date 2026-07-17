#!/usr/bin/env bash
# #1: a buffer with stride < width*4 must be rejected, not read past the mmap.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
await 50 in_log "shm stride" || { echo "compositor did not reject the bad stride"; exit 1; }
expect_alive "compositor died on the bad stride"
echo "OK: undersized shm stride rejected, compositor alive"
