#!/usr/bin/env bash
# The rasterized hardware-cursor image must keep per-pixel alpha. The unit
# bypass in the output transform used to write alpha=1.0, turning the cursor
# plane into an opaque 64x64 square around the arrow.
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_FORCE_CURSOR=1 IMWAY_DEBUG_CURSOR=1
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 in_log "cursor image: visible" || {
    echo "no cursor image was rasterized"
    cat "$IMWAY_LOG"
    exit 1
}

visible=$(grep -a "cursor image: visible" "$IMWAY_LOG" | tail -1 |
          sed -E 's/.*visible ([0-9]+).*/\1/')

# the arrow covers a small fraction of the 64x64 plane; a fully opaque
# readback means the output transform dropped the scene alpha
[[ "$visible" -gt 0 && "$visible" -lt 2048 ]] || {
    echo "cursor plane is opaque: visible=$visible of 4096"
    exit 1
}

echo "OK: hardware cursor keeps transparency (visible=$visible)"
