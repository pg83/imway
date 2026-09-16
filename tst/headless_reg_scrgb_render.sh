#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "managed"
# Extended-linear 0.75 at the default 80-nit maximum is 60 nits.
# That is PQ code 117 in the eight high bits of XR30.
await_mean "$XDG_RUNTIME_DIR/scrgb.ppm" 'app_id=client_feat_color_mgmt' \
    '"$n" -gt 40000 && "$r" -ge 115 && "$r" -le 119 &&
     "$g" -ge 115 && "$g" -le 119 && "$b" -ge 115 && "$b" -le 119' ||
    { echo "extended-linear sRGB did not land on PQ 117"; exit 1; }

echo "OK: extended-linear sRGB maps normalized optical values to nits"
