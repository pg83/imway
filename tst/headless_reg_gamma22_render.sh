#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "managed"
await_mean "$XDG_RUNTIME_DIR/gamma22.ppm" 'app_id=client_feat_color_mgmt' \
    '"$n" -gt 40000 && "$r" -ge 106 && "$r" -le 110 &&
     "$g" -ge 106 && "$g" -le 110 && "$b" -ge 106 && "$b" -le 110' ||
    { echo "gamma 2.2 did not land on its expected luminance"; exit 1; }

echo "OK: gamma 2.2 EOTF maps electrical values to absolute nits"
