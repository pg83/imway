#!/usr/bin/env bash
# A compositor started at --scale 1.5 should advertise preferred_scale 180.
# imway-args: --scale 1.5
# xfail: preferred_scale is hardcoded to 120, surface scaling is not wired to uiScale yet
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "preferred_scale does not follow --scale"; exit 1; }
echo "OK: preferred_scale follows the compositor scale"
