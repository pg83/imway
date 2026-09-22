#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_CHAOS=descriptor-pool=1
# Growing the second descriptor pool fails once: the texture that needed it
# goes without (a cell stays blank, or, when that texture was an icon
# resolved in the middle of the flood, no cell does), the next allocation
# grows the pool after all and every recommitted cell is drawn.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
missing="1 0"
. "$(dirname "$0")/texture_flood_case.sh"
