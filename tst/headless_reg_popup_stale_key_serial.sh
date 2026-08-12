#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"
start_client
wait_client "first ready"
ctl "key 30 press"; ctl "key 30 release"
expect_client_ok "stale key serial was accepted after focus transfer"
expect_alive "compositor died on stale popup serial"
input_health_probe
