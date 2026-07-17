#!/usr/bin/env bash
# wl_output + xdg-output advertise a consistent mode/scale/name/logical size.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "output advertisement inconsistent"; exit 1; }
echo "OK: output geometry consistent across wl_output and xdg-output"
