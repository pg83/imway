#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS="dbus-sndbuf=1 dbus-recv-limit=4096"
# The compositor's bus connection under backpressure: a send buffer at the
# kernel's floor and room for 4 KiB of undispatched messages. A reply far larger
# than the buffer goes out in parts as the socket drains (libdbus arms the
# write watch in between), and a burst of calls is read one message at a
# time (libdbus disarms the read watch until each is dispatched). Every
# call is still answered.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "backpressure done"
expect_client_ok "the busy peer went unanswered"
bytes=$(awk '$1 == "menus" { print $4 }' "$CLIENT_LOG")
[[ "$bytes" -gt 16384 ]] || { echo "the GetMenus reply ($bytes bytes) is too small to back up the socket"; exit 1; }
expect_alive "compositor died under bus backpressure"
echo "OK: large replies and bursts pass through a backed-up bus connection"
