#!/usr/bin/env bash
# private-session-bus
# imway-env: XDG_DATA_HOME=./xdg
# imway-pre: mkdir -p xdg/icons/hicolor/scalable/apps
# imway-pre: printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#00ff00"/></svg>' > xdg/icons/hicolor/scalable/apps/imway-tray-green.svg
# imway-pre: printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#0000ff"/></svg>' > xdg/icons/hicolor/scalable/apps/imway-tray-blue.svg
# A tray item naming a themed icon shows that icon in its dock slot rather
# than the pixmap it also sends, and one that needs attention shows its
# named attention icon.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

sni="$IMWAY_TESTS_BIN/client_feat_dock_status_notifier"

# the pixels of one colour inside the dock column
dock_has() { # <r> <g> <b>
    screenshot "$XDG_RUNTIME_DIR/_dock.ppm"
    python3 - "$XDG_RUNTIME_DIR/_dock.ppm" "$@" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); px = f.read(w * h * 3)
want = bytes(int(v) for v in sys.argv[2:5])
n = sum(1 for y in range(h) for x in range(58) if px[(y * w + x) * 3:(y * w + x) * 3 + 3] == want)
sys.exit(0 if n >= 64 else 1)
PY
}

SNI_ID=imway-named SNI_ICON_NAME=imway-tray-green SNI_SERVICE=org.example.ImwayTrayNamed "$sni" >"$XDG_RUNTIME_DIR/a.log" 2>&1 &
await 100 grep -q "layout requested" "$XDG_RUNTIME_DIR/a.log" || { echo "the named item did not register"; cat "$XDG_RUNTIME_DIR/a.log"; exit 1; }
await 100 dock_has 0 255 0 || { echo "the item's named icon is not in the dock"; dump_state | grep '^tray'; exit 1; }
dock_has 255 0 255 && { echo "the pixmap shows although the item names an icon"; exit 1; }

SNI_ID=imway-urgent SNI_STATUS=NeedsAttention SNI_ICON_NAME=imway-tray-green SNI_ATTENTION_ICON_NAME=imway-tray-blue SNI_SERVICE=org.example.ImwayTrayUrgent "$sni" >"$XDG_RUNTIME_DIR/b.log" 2>&1 &
await 100 grep -q "layout requested" "$XDG_RUNTIME_DIR/b.log" || { echo "the urgent item did not register"; cat "$XDG_RUNTIME_DIR/b.log"; exit 1; }
await 100 dock_has 0 0 255 || { echo "the item needing attention does not show its attention icon"; dump_state | grep '^tray'; exit 1; }
dock_has 255 0 255 && { echo "a pixmap shows although both items name icons"; exit 1; }

expect_alive "compositor died resolving tray icon names"
echo "OK: tray items show their named icon, and their attention icon when urgent"
