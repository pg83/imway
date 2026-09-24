#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=icc-eintr=2
# The ICC profile's reads are interrupted by a signal, twice: each is
# retried, and the profile read whole still changes the composition.
set -euo pipefail
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_color_icc"
. "$(dirname "$0")/color_icc_case.sh"
