#!/usr/bin/env bash
# private-session-bus
# Native Wayland appmenu association + the shared DBusMenu client. The client
# is a strict provider: malformed GetLayout calls make it fail.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "registrar roundtrip"
wait_client "global menu mapped"
wait_client "layout revision 1"
wait_mapped

screenshot "$XDG_RUNTIME_DIR/before-menu.ppm"

# The app_id precedes the exported headings. Find File by interaction instead
# of baking font metrics into the test.
file_x=
for x in $(seq 152 8 240); do
    click_at "$x" 10
    if grep -q "about 1" "$CLIENT_LOG"; then
        file_x=$x
        break
    fi
done

[[ -n "$file_x" ]] || {
    echo "global File menu did not open"
    cat "$CLIENT_LOG" "$IMWAY_LOG"
    exit 1
}

screenshot "$XDG_RUNTIME_DIR/file-menu.ppm"
diff_pixels=$(region_diff "$XDG_RUNTIME_DIR/before-menu.ppm" \
    "$XDG_RUNTIME_DIR/file-menu.ppm" 58 0 700 180)
[[ "$diff_pixels" -gt 200 ]] || {
    echo "global menu popup was not rendered ($diff_pixels changed pixels)"
    exit 1
}

# Sweep the popup rows until the lazy Recent submenu is reached. Its
# AboutToShow(TRUE) must cause a revision-2 GetLayout before it is displayed.
for y in 54 58 62 66 70 74; do
    ctl "motion $((file_x + 45)) $y"
    screenshot "$XDG_RUNTIME_DIR/lazy-menu.ppm"
    screenshot "$XDG_RUNTIME_DIR/lazy-menu.ppm"
    grep -q "about 11" "$CLIENT_LOG" && break
done

wait_client "about 11"
wait_client "layout revision 2"

# The revision refresh atomically replaces the model and closes the old menu
# hierarchy. Let that frame settle, reopen File, and activate its first row.
screenshot "$XDG_RUNTIME_DIR/revision-2.ppm"
screenshot "$XDG_RUNTIME_DIR/revision-2.ppm"
click_at "$file_x" 10
click_at "$((file_x + 50))" 38
wait_client "event 10"

wait_client "property and activation signals sent"
wait_client "conform complete"
expect_client_ok "global DBusMenu conform client failed"
expect_alive "compositor died handling global DBusMenu updates"

echo "OK: registrar, native global menu, lazy layout, properties and Event"
