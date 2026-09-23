#!/usr/bin/env bash
# imway-env: IMWAY_CFG_TRACE=1
# set_maximized / unset_maximized must each draw a configure reply, carry the
# MAXIMIZED state correctly and size against the work area reserved by dock.
# The configure trace names every size the layout wants but has not sent.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "maximize requests were left unanswered"
expect_alive "compositor died on set_maximized"
in_log "imway: cfg? desired=" || { echo "the configure trace never named a pending size"; exit 1; }
echo "OK: maximize state and work-area configures"
