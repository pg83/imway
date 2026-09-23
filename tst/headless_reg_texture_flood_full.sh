#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_CHAOS="descriptor-full=1 descriptor-fragmented=1"
# The texture descriptor chain's first pool is full and its second
# fragmented, as a driver reports pools it cannot carve another set from:
# every texture passes both over and lands in a third, nothing is lost.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
missing=0
. "$(dirname "$0")/texture_flood_case.sh"
