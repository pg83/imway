#!/usr/bin/env bash
# xdg-toplevel-drag objects whose drag never starts: a clipboard source
# cannot take one, and attaching and re-attaching before any drag, then
# dropping the object after its source, are no errors.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in selection idle; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong outcome for an idle toplevel drag ($mode)"; exit 1; }
    expect_alive "compositor died on an idle toplevel drag ($mode)"
done

echo "OK: toplevel drag objects outside a drag behave"
