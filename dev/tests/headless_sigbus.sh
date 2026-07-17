#!/usr/bin/env bash
# Robustness: client truncates the shm pool out from under the compositor;
# the compositor must survive (the runner also verifies the clean exit).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || true

sleep 0.5 # let the compositor render a few frames over the truncated pool
kill -0 "$IMWAY_PID" || { echo "compositor died"; exit 1; }
echo "OK: compositor survived the truncated pool"
