#!/usr/bin/env bash
# kill -9 a client that holds a popup grab, then one that is mid-drag with
# the button still down. The compositor must survive both and keep routing
# clicks to a freshly mapped client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

click_checker() { # <message>
    start_client check
    wait_client "checker ready"
    point_at_color 0 255 0 || { echo "checker window not found"; exit 1; }
    sleep 0.3
    screenshot "$XDG_RUNTIME_DIR/_f.ppm" # a frame computes hover before the click
    ctl "button left press"
    sleep 0.1
    ctl "button left release"
    expect_client_ok "$1"
}

# round 1: die while holding a popup grab
start_client grab
wait_client "ready"
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"
wait_client "grabbed"
ctl "button left release"
sleep 0.3
kill -9 "$CLIENT_PID" 2>/dev/null || true
sleep 0.5
expect_alive "compositor died when the grab holder was killed"
click_checker "input did not reach a new client after grab-holder death"

# round 2: die mid-drag, button still held
start_client drag
wait_client "ready"
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"
wait_client "dragging"
ctl "relmotion 5 5"
sleep 0.3
kill -9 "$CLIENT_PID" 2>/dev/null || true
sleep 0.5
expect_alive "compositor died when the drag source was killed"
ctl "button left release"
sleep 0.3
click_checker "input did not reach a new client after drag-source death"

echo "OK: compositor survived grab-holder and drag-source death, input flows"
