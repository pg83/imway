#!/usr/bin/env bash
# Primary selection: set_selection → offer → receive round-trips data.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
ctl "key 30 press"
ctl "key 30 release"

expect_client_ok "primary-selection round-trip failed"
echo "OK: primary selection round-trips the payload"
