#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=sigbus-record=3
# The compositor reads a client's wl_shm memory only under its SIGBUS guard,
# whose per-thread record is allocated on the thread's first access. With
# that allocation failing, an output capture, a screencopy and a toplevel
# icon each cost their client a wl_shm error on the buffer instead of an
# unguarded read, and once the allocation works again a capture lands.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

capture="$IMWAY_TESTS_BIN/client_reg_capture_edges"
icon="$IMWAY_TESTS_BIN/client_reg_toplevel_icon_errors"

"$capture" output-unguarded || { echo "an unguarded output capture was not refused"; exit 1; }
"$capture" wlr-unguarded || { echo "an unguarded screencopy was not refused"; exit 1; }
"$icon" unguarded || { echo "an unguarded icon read was not refused"; exit 1; }
expect_alive "compositor died without its SIGBUS guard"

"$capture" output || { echo "captures did not recover once the guard could be set up"; exit 1; }
expect_alive "compositor died after its SIGBUS guard came back"
echo "OK: shm reads without a SIGBUS guard are refused on the client's buffer"
