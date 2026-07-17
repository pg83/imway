#!/usr/bin/env bash
# #18: unmapping a focused toplevel (null buffer) must send keyboard leave.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "no keyboard leave after unmap"; exit 1; }
echo "OK: unmap handed keyboard focus away with a leave"
