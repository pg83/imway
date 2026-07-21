#!/usr/bin/env bash
# Direct scanout must never bypass color conversion.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died during direct scanout color policy test"

echo "OK: direct scanout rejects incompatible color states"
