#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_CHAOS=descriptor-pool=1
# Growing the second descriptor pool fails once: the cell that needed it
# stays blank, the next allocation grows the pool after all.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
missing=1
. "$(dirname "$0")/texture_flood_case.sh"
