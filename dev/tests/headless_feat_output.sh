#!/usr/bin/env bash
# wl_output + xdg-output advertise a consistent mode/scale/name/logical size.
# imway-args: --mode 1920x1080@75
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "socket imway-test, output 1920x1080@75" || {
    echo "subsystems were constructed with the placeholder output mode"
    exit 1
}

"$IMWAY_CLIENT" || { echo "output advertisement inconsistent"; exit 1; }
echo "OK: output geometry consistent across wl_output and xdg-output"
