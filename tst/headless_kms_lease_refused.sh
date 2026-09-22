#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS="lease=1 resource=wp_drm_lease_connector_v1"
# imway-args: --device auto
# wp-drm-lease over the KMS emulator when the device says no: a connector
# offer that could not be allocated is skipped, a refused lease ends in
# finished and the retry is granted, a released device object is told so,
# and a doubly requested connector is a protocol error.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

"$IMWAY_CLIENT" || { echo "the lease refusals went wrong"; exit 1; }
expect_alive "compositor died on a refused lease"
echo "OK: refused leases and dropped offers reached the client cleanly"
