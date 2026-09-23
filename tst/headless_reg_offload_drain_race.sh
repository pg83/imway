#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_SHM_COPY_STALL_MS=50
# A screenshot drains the wl_shm copy in flight before it composes. When
# that copy finished while the loop was held up (a loaded host; here the
# loop stalls right after handing each copy over) and the command arrived
# in the same stretch, libev holds both wakeups and runs the command first:
# the copy job's own wakeup then comes after the drain has retired the
# copy, and must not complete it a second time (it did, on a copy that no
# longer existed: a null dereference). A client committing a full-output
# buffer on every frame keeps copies starting while screenshots arrive.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "committing"

for round in $(seq 1 8); do
    ctl "screenshot $XDG_RUNTIME_DIR/race-$round.ppm"
    sleep 0.03
done

# every screenshot is written in turn; the last one being there means the
# loop went through all of them
await 300 test -s "$XDG_RUNTIME_DIR/race-8.ppm" || { expect_alive "the compositor died draining copies for screenshots"; echo "the screenshots did not arrive"; exit 1; }
expect_alive "the compositor died draining copies for screenshots"
echo "OK: a screenshot's drained copy completes once"
