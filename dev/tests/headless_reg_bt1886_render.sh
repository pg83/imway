#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/bt1886.ppm"
read -r r g b n < <(surface_mean "$XDG_RUNTIME_DIR/bt1886.ppm" \
    'app_id=client_feat_color_mgmt')
[[ "$n" -gt 40000 && "$r" -ge 111 && "$r" -le 115 && \
   "$g" -ge 111 && "$g" -le 115 && "$b" -ge 111 && "$b" -le 115 ]]

echo "OK: BT.1886 EOTF uses its black and white luminance"
