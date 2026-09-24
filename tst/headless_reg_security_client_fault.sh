#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=security-client=1
# An accepted sandboxed connection gets no client (libwayland out of
# memory): the compositor closes it, the security context keeps listening
# and serves the next one.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_security_accept_fault"
start_client
wait_client "security accept fault done"
expect_client_ok "a connection left without a client broke the security context's listener"
expect_alive
echo "OK: a sandboxed connection without a client is closed, the next one served"
