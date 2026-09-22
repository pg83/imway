#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_CHAOS=client-texture=15
# The view of the first cell's texture cannot be created (the toplevel's
# upload buffer, texture and view take client-texture calls 0-7, the
# cells' shared upload buffer 8-11, the first cell's image 12-14): that
# cell stays blank until its next commit.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
missing=1
. "$(dirname "$0")/texture_flood_case.sh"
