#!/usr/bin/env bash
# security-context: a sandbox that hangs up while its context object lives
# stops the listening socket at once, and the context destroyed afterwards
# goes quietly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "security close done"
expect_client_ok "the compositor kept listening for a sandbox that hung up"
expect_alive "compositor died tearing down a stopped security context"
echo "OK: a hung-up sandbox stopped its listener and its context went quietly"
