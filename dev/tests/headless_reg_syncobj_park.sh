#!/usr/bin/env bash
# Acquire-fence parking: unsignaled explicit-sync acquire points park the
# surface commit on an eventfd instead of stalling the render loop, and the
# parked content resumes when the points signal.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rc=0
"$IMWAY_CLIENT" || rc=$?

if [[ $rc -eq 77 ]]; then
    echo "SKIP: explicit sync unavailable"
    exit 127
fi

[[ $rc -eq 0 ]] || { echo "syncobj park client failed: $rc"; exit "$rc"; }
expect_alive "compositor died on parked explicit-sync commits"
echo "OK: unsignaled acquire points park the commit, not the compositor"
