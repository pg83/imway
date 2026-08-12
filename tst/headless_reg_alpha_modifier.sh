#!/usr/bin/env bash
# wp-alpha-modifier: a multiplier of 0 must blend the surface away.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "opaque"
sleep 0.3
# opaque phase: the surface area reads green
screenshot "$XDG_RUNTIME_DIR/opaque.ppm"
x=$(dump_field 'app_id=alpha-mod' imgx)
y=$(dump_field 'app_id=alpha-mod' imgy)
w=$(dump_field 'app_id=alpha-mod' client_w)
h=$(dump_field 'app_id=alpha-mod' client_h)
read -r r0 g0 b0 n0 < <(surface_mean "$XDG_RUNTIME_DIR/opaque.ppm" 'app_id=alpha-mod')
[[ "$g0" -gt 150 ]] || { echo "surface not green while opaque: $r0 $g0 $b0"; exit 1; }

# release the client into the transparent phase
ctl "key 57 press"
ctl "key 57 release"
wait_client "transparent"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/transp.ppm"
read -r r1 g1 b1 n1 < <(surface_mean "$XDG_RUNTIME_DIR/transp.ppm" 'app_id=alpha-mod')

# with multiplier 0 the green must be substantially gone (background shows)
[[ "$g1" -lt 100 ]] || { echo "alpha multiplier 0 did not blend the surface away: $r1 $g1 $b1"; exit 1; }

echo "OK: alpha modifier blends the surface (opaque g=$g0 -> transparent g=$g1)"
