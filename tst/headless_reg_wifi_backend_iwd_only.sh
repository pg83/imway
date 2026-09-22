#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_SETTINGS=applications.wifi_backend=1;audio.backend=1
# imway-pre: "$IMWAY_TESTS_BIN/client_feat_wifi_nm" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# Wifi restricted to iwd while only NetworkManager is running, and audio
# restricted to sndio while no sndiod runs: the compositor falls back to
# neither NetworkManager nor pulse and runs without wifi or a mixer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

expect_alive "compositor died without its only allowed wifi backend"
! in_log "wifi via" || { echo "a backend other than iwd was picked"; cat "$IMWAY_LOG"; exit 1; }
[[ -z "$(dump_state | grep '^wifi state')" ]] || { echo "wifi came up without iwd"; dump_state; exit 1; }
! grep -q "^get " "$XDG_RUNTIME_DIR/nm.log" || { echo "NetworkManager was asked although only iwd is allowed"; cat "$XDG_RUNTIME_DIR/nm.log"; exit 1; }
in_log "sndiod unreachable" || { echo "sndio was not tried"; cat "$IMWAY_LOG"; exit 1; }
! in_log "pulse mixer" || { echo "pulse was tried although only sndio is allowed"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: iwd-only and sndio-only preferences do not fall back"
