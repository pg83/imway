#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/sdr.ppm"
read -r r g b n < <(surface_mean "$XDG_RUNTIME_DIR/sdr.ppm" \
    'app_id=client_feat_color_mgmt')

# A 10,000-nit BT.2020 green is far outside SDR. It must be desaturated into
# the BT.709 volume, not independently clipped to electric green.
[[ "$n" -gt 40000 ]]
[[ "$r" -gt 200 && "$g" -gt 200 && "$b" -gt 200 ]]
[[ $((r - g)) -ge -8 && $((r - g)) -le 8 ]]
[[ $((b - g)) -ge -8 && $((b - g)) -le 8 ]]

echo "OK: HDR wide-gamut highlight maps into SDR volume"
