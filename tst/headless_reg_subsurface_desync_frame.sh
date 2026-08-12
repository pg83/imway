#!/usr/bin/env bash
# set_desync must release a cached callback-only commit without a parent commit.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "subsurface desync frame ready"
screenshot "$XDG_RUNTIME_DIR/desync-frame.ppm"
expect_client_ok "set_desync stranded cached non-buffer state"
echo "OK: set_desync applied cached frame state immediately"
