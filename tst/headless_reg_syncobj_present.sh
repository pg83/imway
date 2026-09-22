#!/usr/bin/env bash
# linux-drm-syncobj on shown content: a replaced buffer's release point
# signals, a bufferless commit needs no points, a buffer dropped from a
# synchronized subsurface's cache is released too, and the syncobj objects
# can go while their surfaces live.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rc=0
"$IMWAY_CLIENT" || rc=$?

if [[ $rc -eq 77 ]]; then
    echo "SKIP: explicit sync unavailable"
    exit 127
fi

[[ $rc -eq 0 ]] || { echo "syncobj present client failed: $rc"; exit "$rc"; }
expect_alive "compositor died presenting explicit-sync content"
echo "OK: explicit-sync release points signaled for replaced and dropped buffers"
