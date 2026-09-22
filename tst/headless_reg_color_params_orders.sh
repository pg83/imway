#!/usr/bin/env bash
# Parametric descriptions with the rarer named primaries and luminance
# orders become ready, and the colour objects that die on request go quietly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "a colour description or destroy went wrong"; exit 1; }
expect_alive "compositor died on colour params"
echo "OK: the rarer colour params became ready"
