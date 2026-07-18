#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rc=0
"$IMWAY_CLIENT" || rc=$?

if [[ $rc -eq 77 ]]; then
    echo "SKIP: xdg-toplevel-icon unavailable"
    exit 127
fi

[[ $rc -eq 0 ]] || { echo "icon multi-watch client failed: $rc"; exit "$rc"; }
expect_alive "compositor died on the multi-watched icon buffer"
