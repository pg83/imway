#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_SETTINGS=notifications.wifi=0
# imway-pre: "$IMWAY_TESTS_BIN/client_reg_wifi_nm_edges" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# Wifi notifications switched off: the network connects and drops again
# without a single toast.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

NM_LOG="$XDG_RUNTIME_DIR/nm.log"

wifi_is() { [[ "$(dump_field '^wifi state' state)" == "$1" ]]; }

await 150 grep -q "devices 1" "$NM_LOG" || { echo "the device list was never asked for"; cat "$NM_LOG"; exit 1; }
touch "$XDG_RUNTIME_DIR/go-1"
await 150 wifi_is 4 || { echo "the network did not connect"; dump_state; exit 1; }
touch "$XDG_RUNTIME_DIR/go-2"
await 150 grep -q "devices 4" "$NM_LOG" || { echo "the drop was never asked for"; cat "$NM_LOG"; exit 1; }
await 150 wifi_is 0 || { echo "the network did not drop"; dump_state; exit 1; }
[[ "$(dump_field '^notifications' history)" == 0 ]] || { echo "a toast was posted with wifi notifications off"; dump_state; exit 1; }
echo "OK: wifi notifications off post nothing"
