#!/usr/bin/env bash
# One input method per seat: the second one is inert and destroying it leaves
# the first, and the connection, alone.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" > "$CLIENT_LOG" 2>&1 || { echo "the input-method client failed"; cat "$CLIENT_LOG"; exit 1; }
grep -q "second input method inert" "$CLIENT_LOG" || { echo "no verdict from the client"; cat "$CLIENT_LOG"; exit 1; }
expect_alive "compositor died on a second input method"
echo "OK: the second input method is inert"
