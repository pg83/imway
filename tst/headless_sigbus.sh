#!/usr/bin/env bash
# Robustness: client truncates the shm pool out from under the compositor;
# the compositor must survive (the runner also verifies the clean exit).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || true # runs to completion: commit over a truncated pool

sleep 0.5 # let the compositor render a few frames over it
expect_alive "compositor died on the truncated pool"
echo "OK: compositor survived the truncated pool"
