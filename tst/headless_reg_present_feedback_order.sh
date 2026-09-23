#!/usr/bin/env bash
# A client disconnecting with a pending presentation feedback whose id is
# below its surface's: the feedback is torn down first and leaves the
# surface's list; the compositor then serves the next client as before.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for _ in 1 2 3; do
    "$IMWAY_CLIENT" || { echo "the feedback-order client failed"; exit 1; }
done

expect_alive "compositor died tearing down a pending feedback before its surface"
"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the compositor stopped serving"; exit 1; }
echo "OK: a pending feedback torn down before its surface left it cleanly"
