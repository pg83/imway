#!/usr/bin/env bash
# A compositor started at --scale 1.5 should advertise preferred_scale 180.
# imway-args: --scale 1.5
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "preferred_scale does not follow --scale"; exit 1; }
echo "OK: preferred_scale follows the compositor scale"
