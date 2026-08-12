#!/usr/bin/env bash
# #15: a rejected drag from a data-device v1 client must still get cancelled.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "v1 data source never got cancelled"; exit 1; }
input_health_probe
echo "OK: wl_data_source.cancelled delivered at version 1"
