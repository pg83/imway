#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wp_drm_lease_device_v1 resource=wp_drm_lease_request_v1 resource=wp_drm_lease_v1"
# wp-drm-lease objects that fail to allocate over the KMS emulator: the
# device bind, the lease request and the lease itself each reach their
# client as no_memory, and the compositor lives on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

for run in device request lease; do
    "$IMWAY_CLIENT" "$run" || { echo "$run: the failed allocation did not reach the client as no_memory"; exit 1; }
    expect_alive "compositor died on a failed $run allocation"
done

echo "OK: failed lease allocations reached their clients"
