#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=security-accept=1
# The accept of a sandboxed connection fails: that client is dropped, the
# security context keeps listening and serves the next one.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "security accept fault done"
expect_client_ok "a failed accept broke the security context's listener"
expect_alive
echo "OK: a failed sandbox accept drops only that connection"
