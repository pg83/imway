#!/usr/bin/env bash
# The control FIFO taken away from under the compositor: when its last
# writer goes, the compositor reopens the path to wait for the next one, and
# with nothing there it gives control up rather than failing. It stops
# reading the old FIFO, and it still serves clients.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

moved="$XDG_RUNTIME_DIR/ctl-moved"
mv "$IMWAY_CTL" "$moved"
exec 3>&- # the scenario's writer goes: EOF, and the reopen finds no FIFO

no_reader() { ! python3 -c 'import os, sys; os.open(sys.argv[1], os.O_WRONLY | os.O_NONBLOCK)' "$moved" 2>/dev/null; }
await 50 no_reader || { echo "the compositor still reads the moved FIFO"; exit 1; }
expect_alive "compositor died losing its control FIFO"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_dock"
start_client
wait_client "dock client ready"
wait_mapped

expect_alive "compositor died serving a client without control"
echo "OK: a vanished control FIFO ends control and nothing else"
