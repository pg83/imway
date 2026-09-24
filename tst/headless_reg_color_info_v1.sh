#!/usr/bin/env bash
# A colour-management v1 client asks for an SDR output's description: the
# transfer function comes by the name v1 has for sRGB, not the compound
# power 2.4 a v2 client is told.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_color_feedback_v1"
start_client info
wait_client "info v1 done"
expect_client_ok "a v1 client was told the v2 name of sRGB"
expect_alive "compositor died describing the output to a v1 client"
echo "OK: a v1 client hears sRGB by its v1 name"
