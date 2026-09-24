#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_PRIME=1
# The dumb-buffer path already reads every frame back for presentation; the
# eyedropper samples that copy instead of reading the frame back again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
in_log "dumb-buffer path (no zero-copy scanout)" || { echo "no dumb-buffer fallback"; cat "$IMWAY_LOG"; exit 1; }
. "$(dirname "$0")/color_picker_case.sh"
