#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=frame-busy=5
# The last frame's fence still reads busy when the next frame is due, five
# times: each time the frame waits for the next tick instead of racing the
# GPU, and the session keeps composing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

frames() { dump_field '^frames' done; }
f0=$(frames)
advanced() { [[ "$(frames)" -ge $((f0 + 8)) ]]; }
for i in $(seq 1 60); do
    ctl "motion $((200 + i * 3)) 300"
    advanced && break
    sleep 0.05
done
advanced || { echo "frames stalled behind a busy frame fence ($f0 -> $(frames))"; exit 1; }

expect_alive "compositor died waiting on a busy frame fence"
echo "OK: a frame fence still busy puts the frame off to the next tick"
