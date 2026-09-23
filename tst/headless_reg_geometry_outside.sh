#!/usr/bin/env bash
# A window geometry inside the surface sizes the window; one that starts
# past the surface's right and bottom edges has nothing to crop to, and the
# window falls back to the whole surface instead of a size of its own.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }
size_is() { [[ "$(dump_field 'app_id=geometry-outside' client_w)x$(dump_field 'app_id=geometry-outside' client_h)" == "$1" ]]; }

start_client
wait_client "inside"
await 100 size_is 50x40 || { echo "the inner geometry did not size the window"; dump_state; exit 1; }
go inside

wait_client "outside"
await 100 size_is 200x120 || { echo "a geometry outside the surface did not fall back to the surface"; dump_state; exit 1; }
go outside

wait_client "geometry outside done"
expect_client_ok "the geometry client failed"
echo "OK: a geometry outside the surface falls back to the whole surface"
