#!/usr/bin/env bash
# Arguments that pass every check but the last: a negative buffer transform,
# a size, geometry, positioner or anchor rectangle with only its height out of
# range, viewport source rectangles off by one coordinate and destinations
# off by one dimension; a scale the shown buffer cannot take, a crop past the
# bottom edge only, a buffer after an unacknowledged initial configure, a
# subsurface placed relative to itself, a subsurface of a surface an
# xdg_surface claimed, a minimum height over the maximum. Each
# is its protocol's error; the compositor lives on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

client="$IMWAY_TESTS_BIN/client_wl_misc"

for what in transform geometry max-size min-size positioner-size anchor-rect \
            source-x source-y source-h destination-w destination-h \
            rescale source-y-outside unacked place-self subsurface-of-xdg min-over-max-height; do
    "$client" "bad-$what" || { echo "bad-$what was not the expected protocol error"; exit 1; }
    expect_alive "the compositor died on bad-$what"
done
echo "OK: every edge argument was its protocol error"
