#!/usr/bin/env bash
# A second compositor asked for the socket the running one holds cannot
# add it: it reports the socket it failed on and exits nonzero, and the
# running compositor keeps serving.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
rc=0
out=$(timeout 60 "$imway_bin" --device headless --socket imway-test --frames 3 2>&1) || rc=$?

[[ "$rc" -ne 0 ]] || { echo "a second compositor took a socket that was in use: $out"; exit 1; }
grep -q "wl socket imway-test failed" <<<"$out" || { echo "the taken socket was not reported (rc=$rc): $out"; exit 1; }

expect_alive "the running compositor died when a second one failed its socket"
"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the running compositor stopped serving"; exit 1; }
echo "OK: a taken socket stopped the second compositor, not the first"
