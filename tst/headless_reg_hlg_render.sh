#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "managed"
# HLG 0.75 is its 203-nit reference white on the nominal 1000-nit system.
# That is PQ code 148 in the eight high bits of XR30.
await_mean "$XDG_RUNTIME_DIR/hlg.ppm" 'app_id=client_feat_color_mgmt' \
    '"$n" -gt 40000 && "$r" -ge 146 && "$r" -le 150 &&
     "$g" -ge 146 && "$g" -le 150 && "$b" -ge 146 && "$b" -le 150' ||
    { echo "HLG reference white did not land on PQ 148"; exit 1; }

echo "OK: HLG reference white decodes to 203 nits"
