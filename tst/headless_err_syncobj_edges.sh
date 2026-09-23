#!/usr/bin/env bash
# Explicit-sync points committed with no buffer: a release point alone, and
# both points next to an attach of no buffer, are each no_buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in release-only null-attach; do
    rc=0
    "$IMWAY_CLIENT" "$mode" || rc=$?

    if [[ $rc -eq 77 ]]; then
        echo "SKIP: explicit sync unavailable"
        exit 127
    fi

    [[ $rc -eq 0 ]] || { echo "wrong/no error for $mode"; exit 1; }
    expect_alive "compositor died on explicit-sync points without a buffer ($mode)"
done

echo "OK: explicit-sync points without a buffer were refused"
