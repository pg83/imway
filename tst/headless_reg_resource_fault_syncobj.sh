#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="resource=wp_linux_drm_syncobj_manager_v1 resource=wp_linux_drm_syncobj_surface_v1 resource=wp_linux_drm_syncobj_timeline_v1"
# The syncobj manager's bind, a syncobj surface and an imported timeline each
# fail to allocate once: the client asking gets no_memory, the compositor
# lives on. Skips where explicit sync is unavailable.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

client="$IMWAY_TESTS_BIN/client_resource_fault_sync"
for mode in sync-bind sync-surface sync-timeline; do
    rc=0
    "$client" "$mode" || rc=$?
    if [[ $rc -eq 77 ]]; then
        echo "SKIP: explicit sync unavailable"
        exit 127
    fi
    [[ $rc -eq 0 ]] || { echo "$mode: the failed allocation did not reach the client as no_memory"; exit 1; }
    expect_alive "compositor died on a failed allocation in $mode"
done
echo "OK: failed syncobj allocations reached their clients"
