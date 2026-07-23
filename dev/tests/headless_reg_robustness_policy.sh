#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died during robustness policy test"
echo "OK: restart backoff and scanout failure policy"
