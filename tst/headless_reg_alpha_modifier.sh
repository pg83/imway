#!/usr/bin/env bash
# wp-alpha-modifier: a multiplier of 0 must blend the surface away.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "opaque"
# opaque phase: the surface area reads green
read -r r0 g0 b0 _ < <(await_mean "$XDG_RUNTIME_DIR/opaque.ppm" 'app_id=alpha-mod' \
    '"$g" -gt 150') || { echo "surface not green while opaque"; exit 1; }

# release the client into the transparent phase
ctl "key 57 press"
ctl "key 57 release"
wait_client "transparent"

# with multiplier 0 the green must be substantially gone (background shows)
read -r r1 g1 b1 _ < <(await_mean "$XDG_RUNTIME_DIR/transp.ppm" 'app_id=alpha-mod' \
    '"$g" -lt 100') || { echo "alpha multiplier 0 did not blend the surface away"; exit 1; }

echo "OK: alpha modifier blends the surface (opaque g=$g0 -> transparent g=$g1)"
