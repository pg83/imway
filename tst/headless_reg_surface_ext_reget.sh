#!/usr/bin/env bash
# Per-surface extension objects destroyed while the surface lives let go of
# it (a second one is no error); an orphaned constraint takes a region and
# its destroy quietly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "surface extensions done"
expect_client_ok "a surface extension object did not let go of its surface"
expect_alive "compositor died releasing surface extension objects"
echo "OK: surface extension objects came and went cleanly"
