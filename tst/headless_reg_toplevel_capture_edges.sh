#!/usr/bin/env bash
# Toplevel capture of a window that grows, unmaps, dies or does not fit the
# output: frames bounce with fresh constraints, sessions stop, and a dead
# window's handle never turns into a capture of the whole output. A window
# whose geometry is inset in its buffer is captured from its geometry.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "toplevel capture edges done"
expect_client_ok "toplevel capture mishandled a window that changed under it"
expect_alive "compositor died on a toplevel capture edge case"
echo "OK: toplevel capture followed its window through resize, unmap and death"
