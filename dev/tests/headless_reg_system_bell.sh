#!/usr/bin/env bash
# xdg-system-bell: ring must register (bell count advances) and keep the
# compositor alive.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "rang"
sleep 0.2

count=$(dump_field 'bell' count)
[[ "${count:-0}" -ge 2 ]] || { echo "ring did not register (bell count=$count)"; exit 1; }
expect_alive

echo "OK: xdg-system-bell ring registered (count=$count)"
