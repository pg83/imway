#!/usr/bin/env bash
# A manually-built ImDrawList must bind the font atlas texture; otherwise the
# hardware cursor bitmap is transparent after ImGui's TextureId -> TexRef API.
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_FORCE_CURSOR=1 IMWAY_DEBUG_CURSOR=1
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 grep -Eq 'cursor image: visible [1-9][0-9]*, rgb [1-9]' "$IMWAY_LOG" || {
    echo "hardware cursor rasterized to an empty image"
    cat "$IMWAY_LOG"
    exit 1
}

echo "OK: hardware cursor raster contains visible color pixels"
