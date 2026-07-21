#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-min 0.02 --hdr-peak 700 --hdr-fall 350 --bpc 12 --rgb-range limited
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# Protocol min luminance is in 0.0001 nit units; max is in whole nits.
"$IMWAY_CLIENT" info-volume 200 700
expect_alive "compositor died with display color overrides"

echo "OK: display color overrides reach the output contract"
