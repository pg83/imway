#!/usr/bin/env bash
# set_maximized / unset_maximized must each draw a configure reply, carry the
# MAXIMIZED state correctly and size against the work area reserved by dock.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "maximize requests were left unanswered"
expect_alive "compositor died on set_maximized"
echo "OK: maximize state and work-area configures"
