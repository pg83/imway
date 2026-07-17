#!/usr/bin/env bash
# A buffered grandchild is still unmapped when its immediate parent is unmapped.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "nested surface output leave did not follow ancestor unmap"
echo "OK: nested surface left output with its unmapped ancestor"
