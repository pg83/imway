#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "render fault ready"

id=$(dump_field 'title=render-fault-victim' id)
[[ -n "$id" ]] || { echo "render-fault victim not found"; exit 1; }
ctl "render-fault $id"
expect_client_ok "faulted client was not disconnected"

"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "renderer fault escaped the owning client"
echo "OK: Scene render-fault queue disconnects only the owning client"
