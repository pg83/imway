#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS='sync-file=5 sync-wait=16'
# The sixth implicit sync file cannot be exported, and the extra semaphore
# the frame needs for its seventeenth wait cannot be created.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/dmabuf_sync_waits_case.sh"
