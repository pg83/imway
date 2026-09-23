#!/usr/bin/env bash
# A cursor-shape device for a tablet tool takes a shape without error and
# without touching the pointer cursor; a wl_touch is handed out and released.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

before=$(dump_field '^cursor ' shape)
"$IMWAY_CLIENT" || { echo "the tablet tool cursor client failed"; exit 1; }
after=$(dump_field '^cursor ' shape)
[[ "$before" == "$after" ]] || { echo "the tool's shape moved the pointer cursor ($before -> $after)"; exit 1; }

expect_alive "compositor died on a tablet tool cursor device"
echo "OK: the tablet tool's shape device and the touch object came and went"
