#!/usr/bin/env bash
# presentation-time: a committed frame is reported presented, with a clock_id.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "presentation feedback not delivered"
echo "OK: presentation-time reported the frame as presented"
