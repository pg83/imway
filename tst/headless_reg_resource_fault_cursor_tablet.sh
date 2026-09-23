#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=resource=wp_cursor_shape_device_v1
# The cursor-shape device of a tablet tool fails to allocate: the client
# gets no_memory, the compositor lives on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_TESTS_BIN/client_resource_fault_sync" cursor-tablet || { echo "the failed tablet-tool cursor device was not no_memory"; exit 1; }
expect_alive "compositor died on a failed tablet-tool cursor device"
echo "OK: a failed tablet-tool cursor device reached its client"
