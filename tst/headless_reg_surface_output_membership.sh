#!/usr/bin/env bash
# A visible surface enters the sole headless output and leaves it on unmap.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "output membership ready"
wait_client "surface entered output"

ctl "key 2 press"; ctl "key 2 release" # KEY_1
expect_client_ok "surface output enter/leave sequence is incomplete"
echo "OK: mapped surface entered the output and unmapped surface left it"
