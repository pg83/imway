#!/usr/bin/env bash
# Output capture that does not end in a ready frame: ext-image-copy-capture
# bounces buffers off the constraints and fails frames whose buffer died,
# zwlr-screencopy clamps an off-output region and fails a copy into a dead
# buffer, and a destination whose memory vanished mid-copy costs its client
# a wl_shm error, not the compositor its life.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in output output-sigbus wlr wlr-sigbus; do
    "$IMWAY_CLIENT" "$mode" || { echo "capture edge case $mode went wrong"; exit 1; }
    expect_alive "compositor died on capture edge case $mode"
done

echo "OK: capture failures reached their clients and the compositor stayed up"
