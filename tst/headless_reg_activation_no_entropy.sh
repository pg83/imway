#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=entropy=4
# Early at boot getrandom has no entropy yet: activation tokens take their
# random part from the clock and a counter instead, and still activate.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_activation"
start_client
wait_mapped
ctl "key 30 press"   # a serial for the activation token
ctl "key 30 release"

expect_client_ok "a token made without entropy did not activate"
echo "OK: activation works with tokens made without entropy"
