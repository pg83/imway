#!/usr/bin/env bash
# wp-drm-lease over the KMS emulator: the compositor drives the desktop
# connector and offers only the non-desktop one, a client leases it and
# receives a live fd, and the protocol errors are refused.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60, connector 101" || {
    echo "the desktop output did not take the desktop connector"
    cat "$IMWAY_LOG"
    exit 1
}

start_client
wait_client "id 301"
wait_client "empty request refused"
wait_client "leased"
wait_client "duplicate refused"
expect_client_ok "the lease client failed"

# the compositor keeps flipping its own pipe while a lease is out
flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "the desktop pipe stopped flipping"; exit 1; }

expect_alive "compositor died leasing a connector"
echo "OK: the non-desktop connector leases out, the desktop one keeps flipping"
