#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_EDID=garbage
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client

# the raw phase holds for three seconds, long enough to catch its frame
wait_client "raw"
await_mean "$XDG_RUNTIME_DIR/raw.ppm" 'app_id=client_feat_color_mgmt' \
    '"$r $g $b" == "121 108 82"' ||
    { echo "the raw surface never reached its expected mean"; exit 1; }

wait_client "managed"
await_mean "$XDG_RUNTIME_DIR/managed.ppm" 'app_id=client_feat_color_mgmt' \
    '"$r $g $b" == "123 107 76"' ||
    { echo "the managed surface never reached its expected mean"; exit 1; }

echo "OK: custom chromaticities are transformed into the BT.2020 scene"
