#!/usr/bin/env bash
# imway-env: IMWAY_FORCE_CURSOR=1
# The eyedropper under a software cursor: the cursor is composited into
# the frame right at the sampled pixel, and the pick must read the window
# under it, not the cursor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/color_picker_case.sh"
