#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "hdr-headroom ready"
ctl "sdr-white 5000"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/clamped.ppm"
read -r r g b n < <(surface_mean "$XDG_RUNTIME_DIR/clamped.ppm" \
    'app_id=hdr-headroom')

# sRGB 128 decodes to 21.6% linear. With SDR white clamped to the calibrated
# 600-nit peak this is 129.5 nits, or PQ8 code 136. Without the policy the
# impossible 5000-nit request tone-maps the surface to code 178 near peak.
[[ "$n" -gt 40000 ]]
[[ "$r" -ge 134 && "$r" -le 138 ]]
[[ "$g" -ge 134 && "$g" -le 138 ]]
[[ "$b" -ge 134 && "$b" -le 138 ]]

for _ in $(seq 1 10); do
    ctl "key 224 press"   # KEY_BRIGHTNESSDOWN
    ctl "key 224 release"
done
sleep 1.7
screenshot "$XDG_RUNTIME_DIR/lower-white.ppm"
read -r r g b n < <(surface_mean "$XDG_RUNTIME_DIR/lower-white.ppm" \
    'app_id=hdr-headroom')
[[ "$r" -ge 129 && "$r" -le 134 ]]
[[ "$g" -ge 129 && "$g" -le 134 ]]
[[ "$b" -ge 129 && "$b" -le 134 ]]

echo "OK: SDR white is bounded by calibrated HDR peak and owns HDR brightness keys"
