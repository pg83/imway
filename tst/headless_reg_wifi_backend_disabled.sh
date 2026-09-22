#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_SETTINGS=applications.wifi_backend=3;audio.backend=3
# imway-pre: "$IMWAY_TESTS_BIN/client_feat_wifi_nm" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# Wifi and audio switched off in the settings: a NetworkManager on the bus
# is left alone, no mixer backend is even tried, and the bar has no wifi.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

expect_alive "compositor died with its services disabled"
! in_log "wifi via" || { echo "a disabled wifi backend was picked"; cat "$IMWAY_LOG"; exit 1; }
! in_log "sndiod\|pulse" || { echo "a disabled audio backend was tried"; cat "$IMWAY_LOG"; exit 1; }
[[ -z "$(dump_state | grep '^wifi state')" ]] || { echo "the disabled wifi still reports a state"; dump_state; exit 1; }
[[ "$(dump_field '^wifi glyph' x0)" == -1 ]] || { echo "the bar drew a wifi glyph"; dump_state; exit 1; }
! grep -q "^get " "$XDG_RUNTIME_DIR/nm.log" || { echo "the disabled backend still asked NetworkManager"; cat "$XDG_RUNTIME_DIR/nm.log"; exit 1; }
echo "OK: disabled wifi and audio backends stay off"
