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
# The default layout lands File at 208; try it first so the conform test does
# not spend most of its timeout rendering search clicks under a loaded suite.
# Keep the full sweep as a fallback for different fonts/scales.
for x in 208 $(seq 152 8 240); do
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

# File once more, and a second click on the open heading closes its popup
menu_shown() {
    screenshot "$XDG_RUNTIME_DIR/menu-now.ppm" &&
        (( $(region_diff "$XDG_RUNTIME_DIR/before-menu.ppm" "$XDG_RUNTIME_DIR/menu-now.ppm" 58 30 700 180) > 200 ))
}
menu_hidden() { ! menu_shown; }
click_at "$file_x" 10
await 50 menu_shown || { echo "File did not open again"; exit 1; }
click_at "$file_x" 10
await 50 menu_hidden || { echo "a second click on File did not close its popup"; exit 1; }

# Help is a leaf right on the bar, the heading after File: a click
# activates it with no popup. Its place comes from the bar's own pixels:
# the run of label text after the one File's click point sits in (a fixed
# offset from File depends on the font). The client finishes only once it
# has seen both activations.
help_x=$(python3 - "$XDG_RUNTIME_DIR/before-menu.ppm" "$file_x" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
fx = int(sys.argv[2])
def ink(x):
    return any(min(d[(y*w+x)*3:(y*w+x)*3+3]) > 150 for y in range(3, 19))
# label runs: text columns, gaps under 6px bridged
runs, start, gap, end = [], None, 0, 0
for x in range(58, min(w, fx + 300)):
    if ink(x):
        if start is None:
            start = x
        gap, end = 0, x
    elif start is not None:
        gap += 1
        if gap >= 6:
            runs.append((start, end))
            start, gap = None, 0
if start is not None:
    runs.append((start, end))
# File is the first run that ends past the click point (which may sit in
# the heading's padding, left of its text); Help is the one after it
i = next(i for i, (a, b) in enumerate(runs) if b >= fx)
a, b = runs[i + 1]
print((a + b) // 2)
PY
) || { echo "no heading after File in the bar"; exit 1; }
echo "File at $file_x, Help at $help_x"
help=0
for _ in 1 2 3; do
    click_at "$help_x" 10
    if await 50 grep -q "event 2$" "$CLIENT_LOG"; then
        help=1
        break
    fi
done
(( help )) || { echo "the bar's Help leaf did not activate"; cat "$CLIENT_LOG"; exit 1; }

wait_client "property and activation signals sent"
wait_client "conform complete"
expect_client_ok "global DBusMenu conform client failed"
expect_alive "compositor died handling global DBusMenu updates"

echo "OK: registrar, native global menu, lazy layout, properties and Event"
