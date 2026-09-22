#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto --hdr 300
# On a 10-bit HDR scanout the eyedropper narrows the sampled pixel from
# A2R10G10B10 to the 8-bit colour it shows.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
in_log "10-bit scanout" || { echo "no 10-bit scanout"; cat "$IMWAY_LOG"; exit 1; }
swatch_white=300
. "$(dirname "$0")/color_picker_case.sh"
