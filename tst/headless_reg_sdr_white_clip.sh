#!/usr/bin/env bash
# SDR white must reach full code on an SDR output. The display tone map used
# to compress everything above 0.9*peak unconditionally, so a plain white
# window rendered as 249/255 even with no HDR content anywhere on screen.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

read -r r g b n < <(surface_mean "$XDG_RUNTIME_DIR/shot.ppm" "app_id=sdr-white-clip")
[[ "$r" -ge 252 && "$g" -ge 252 && "$b" -ge 252 ]] || {
    echo "SDR white is dimmed by the tone map: mean $r $g $b over $n px"
    exit 1
}

echo "OK: SDR white reaches full code without HDR content"
