#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "raw"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/raw.ppm"
wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/managed.ppm"
read -r rr rg rb rn < <(surface_mean "$XDG_RUNTIME_DIR/raw.ppm" \
    'app_id=client_feat_color_mgmt')
read -r mr mg mb mn < <(surface_mean "$XDG_RUNTIME_DIR/managed.ppm" \
    'app_id=client_feat_color_mgmt')
[[ "$rr $rg $rb" == "121 108 82" ]]
[[ "$mr $mg $mb" == "123 107 76" ]]
echo "OK: Display P3 primaries are transformed into the BT.2020 scene"
