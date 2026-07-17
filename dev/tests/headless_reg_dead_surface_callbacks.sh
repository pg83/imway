#!/usr/bin/env bash
# Frame callbacks / presentation feedback on surfaces destroyed before the
# frame shows: no crash, and callbacks still fire for the next window.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "bare surface destroyed"
wait_client "mapped toplevel destroyed"
expect_client_ok "callback machinery broke after dead-surface callbacks"
expect_alive "compositor died on callbacks of a dead surface"
echo "OK: dead-surface callbacks are dropped cleanly, live ones keep firing"
