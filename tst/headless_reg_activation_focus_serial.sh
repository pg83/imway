#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "activation replay attempted"
# focus is per-frame renderer truth; poll until the handoff reaches a frame
focused=""
for _ in $(seq 1 30); do
    sleep 0.2
    focused=$(dump_state | awk '/focused=1/ { for (i=1;i<=NF;i++) if ($i ~ /^app_id=/) { sub(/^app_id=/, "", $i); print $i; exit } }')
    [[ $focused == activation-new-focus ]] && break
done
[[ $focused == activation-new-focus ]] || {
    echo "stale activation serial stole focus: $focused"; exit 1; }
kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true
input_health_probe
