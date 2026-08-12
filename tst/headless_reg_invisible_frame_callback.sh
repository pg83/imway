#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died during invisible frame callback test"
echo "OK: frame callbacks follow actual presentation"
