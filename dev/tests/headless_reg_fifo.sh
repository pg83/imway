#!/usr/bin/env bash
# #C-7: wp-fifo queues one content update per presentation. The client
# asserts the per-commit frame callbacks land in distinct frames; the final
# screenshot confirms the last queued buffer (magenta) reached the screen.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "fifo done"
sleep 0.3

screenshot "$XDG_RUNTIME_DIR/_f.ppm"
centroid "$XDG_RUNTIME_DIR/_f.ppm" 255 0 255 >/dev/null || {
    echo "the last queued buffer never reached the screen"
    exit 1
}
echo "OK: fifo applied one queued update per frame, ending on the last buffer"
