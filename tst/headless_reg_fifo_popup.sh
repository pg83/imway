#!/usr/bin/env bash
# wp-fifo on a popup: a mapped popup counts as presented, so its barrier
# holds and its queued updates apply one per frame, like a toplevel's. The
# client asserts the frame callbacks land in distinct frames.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "fifo popup done"
expect_client_ok "the popup's fifo did not queue its updates"
expect_alive "compositor died queueing a popup's fifo updates"
echo "OK: a popup's fifo applied one queued update per frame"
