#!/usr/bin/env bash
# Shared scene/output color model invariants.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died during color model test"

echo "OK: shared color model invariants"
