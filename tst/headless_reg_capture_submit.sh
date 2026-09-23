#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=capture-submit=3
# The GPU queue refusing the frame capture's copy: a screencopy retries on
# the next frames, and after its three attempts are refused it fails
# cleanly. The next client's copy, with the queue taking work again,
# completes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

copier="$IMWAY_TESTS_BIN/client_reg_screencopy_bare"

rc=0
"$copier" "$XDG_RUNTIME_DIR/refused.ppm" >"$XDG_RUNTIME_DIR/first.log" 2>&1 || rc=$?
[[ "$rc" -eq 1 ]] || { echo "a copy whose submits were all refused exited $rc"; cat "$XDG_RUNTIME_DIR/first.log"; exit 1; }
grep -q "screencopy failed" "$XDG_RUNTIME_DIR/first.log" || { echo "the refused copy did not fail"; cat "$XDG_RUNTIME_DIR/first.log"; exit 1; }
[[ "$(grep -c "capture submit failed" "$IMWAY_LOG")" -eq 3 ]] || { echo "not three refused attempts"; cat "$IMWAY_LOG"; exit 1; }

"$copier" "$XDG_RUNTIME_DIR/copied.ppm" >"$XDG_RUNTIME_DIR/second.log" 2>&1 || { echo "the next copy failed"; cat "$XDG_RUNTIME_DIR/second.log"; exit 1; }
[[ "$(head -c 2 "$XDG_RUNTIME_DIR/copied.ppm")" == "P6" ]] || { echo "the next copy wrote no image"; exit 1; }

expect_alive "compositor died on refused capture submits"
echo "OK: refused capture submits are retried, then fail the copy cleanly"
