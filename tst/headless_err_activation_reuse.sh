#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# every mutation of a committed token is refused, and so is a second commit
for late in app-id serial surface commit; do
    "$IMWAY_CLIENT" "$late" || { echo "a committed token took $late"; exit 1; }
    expect_alive "compositor died on reused activation token ($late)"
done
