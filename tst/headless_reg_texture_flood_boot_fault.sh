#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_CHAOS=descriptor-pool=0
# The first descriptor pool cannot be created at boot: the first texture
# grows one on demand and nothing is lost.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
missing=0
. "$(dirname "$0")/texture_flood_case.sh"
