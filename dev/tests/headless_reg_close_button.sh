#!/usr/bin/env bash
# The SSD title-bar close button turns into xdg_toplevel.close and the
# window disappears once the client obeys.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "close client mapped"
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

x=$(dump_field 'app_id=closable' x); y=$(dump_field 'app_id=closable' y)
w=$(dump_field 'app_id=closable' w)
imgy=$(dump_field 'app_id=closable' imgy)

# the close button sits at the right end of the title bar
cx=$((x + w - 14)); cy=$(((y + imgy) / 2))
click_at "$cx" "$cy"

wait_client "close received"
expect_client_ok "close button did not close"
sleep 0.4

# and the window is really gone
[[ -z $(dump_field 'app_id=closable' mapped) ]] || {
    echo "window still in the scene after close"; exit 1; }
expect_alive "compositor died on the close button"
echo "OK: title-bar close button delivered xdg_toplevel.close"
