#!/usr/bin/env bash
# xdg-toplevel-icon: a client icon is accepted; a bad-stride icon is ignored.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "toplevel icon rejected or crashed the compositor"
expect_alive "compositor died on a malformed icon buffer"
echo "OK: toplevel-icon accepted a good icon and survived a bad one"
