#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_SETTINGS=applications.wifi_backend=1
# imway-pre: "$IMWAY_TESTS_BIN/client_feat_wifi_nm" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# Wifi restricted to iwd while only NetworkManager is running: the
# compositor does not fall back to it and runs without wifi.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

expect_alive "compositor died without its only allowed wifi backend"
! in_log "wifi via" || { echo "a backend other than iwd was picked"; cat "$IMWAY_LOG"; exit 1; }
[[ -z "$(dump_state | grep '^wifi state')" ]] || { echo "wifi came up without iwd"; dump_state; exit 1; }
! grep -q "^get " "$XDG_RUNTIME_DIR/nm.log" || { echo "NetworkManager was asked although only iwd is allowed"; cat "$XDG_RUNTIME_DIR/nm.log"; exit 1; }
echo "OK: an iwd-only preference does not fall back to NetworkManager"
