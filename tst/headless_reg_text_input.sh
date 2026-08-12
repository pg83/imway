#!/usr/bin/env bash
# #F-12/13: text-input-v3 <-> input-method-v2 round trip.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "text-input done"
expect_client_ok "the text-input/input-method bridge did not round-trip"
echo "OK: enabled text input activated the IME and received its commit_string"
