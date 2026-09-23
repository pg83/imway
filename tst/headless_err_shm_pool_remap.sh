#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=shm-map=1
# The pool maps at creation; growing it needs a new mapping, and when mmap
# fails there the client gets WL_SHM_ERROR_INVALID_FD. The compositor lives on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_TESTS_BIN/client_wl_misc" bad-pool-resize-map || { echo "a failed pool remap was not INVALID_FD"; exit 1; }
expect_alive "the compositor died on a failed pool remap"
echo "OK: a pool that cannot be remapped is INVALID_FD"
