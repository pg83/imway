#!/usr/bin/env bash
# #S-16: security-context hides privileged globals from sandboxed clients.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "security-context done"
expect_client_ok "the sandbox filter did not hide the privileged global"
echo "OK: sandboxed clients see the core but not privileged protocols"
