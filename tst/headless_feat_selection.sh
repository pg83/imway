#!/usr/bin/env bash
# wl_data_device clipboard: set_selection → offer → receive round-trips data.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
ctl "key 30 press"   # a serial the client can set the selection with
ctl "key 30 release"

expect_client_ok "clipboard round-trip failed"
echo "OK: wl_data_device selection round-trips the payload"
