#!/usr/bin/env bash
# --scale is compositor UI scale, not Wayland output scale. A client must
# still receive preferred_scale 120 while output coordinates remain 1:1.
# imway-args: --scale 2.5
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "ui scale leaked into fractional output scale"; exit 1; }
echo "OK: compositor UI scale does not inflate client buffers"
