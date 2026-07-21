#!/usr/bin/env bash
# Opaque-coverage policy for alpha-capable direct scanout formats.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died during opaque policy test"

echo "OK: opaque coverage policy holds"
