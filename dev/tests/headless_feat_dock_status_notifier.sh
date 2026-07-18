#!/usr/bin/env bash
# private-session-bus
# StatusNotifier items appear in the dock, load DBusMenu asynchronously and
# route a primary click back to the item.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "registered"
wait_client "layout requested"

point_at_color 255 0 255 || { echo "tray pixmap did not appear"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 255)
[[ "$x" -lt 58 && "$y" -gt 50 ]] || {
    echo "tray icon is outside dock: $x $y"
    exit 1
}

click_at "$x" "$y"
wait_client "activated"
expect_alive "compositor died handling StatusNotifier activation"

# The dock, not the item, renders DBusMenu.  With one menu row the changed
# popup rectangle's centre is the action itself, so click it and verify Event.
ctl "motion 500 500"
screenshot "$XDG_RUNTIME_DIR/no-menu.ppm"
ctl "motion $x $y"
screenshot "$XDG_RUNTIME_DIR/hover.ppm"
ctl "button right press"
sleep 0.1
ctl "button right release"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/menu.ppm"

read -r mx my < <(python3 - "$XDG_RUNTIME_DIR/hover.ppm" "$XDG_RUNTIME_DIR/menu.ppm" <<'PY'
import sys
def load(path):
    f = open(path, 'rb'); assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split()); f.readline()
    return w, h, f.read(w*h*3)
w, h, a = load(sys.argv[1]); _, _, b = load(sys.argv[2])
pts = []
for y in range(h):
    for x in range(58, min(w, 500)):
        i = (y*w+x)*3
        if sum(abs(a[i+j]-b[i+j]) for j in range(3)) > 40:
            pts.append((x, y))
assert len(pts) > 100, 'dock did not render DBusMenu'
print((min(x for x, _ in pts)+max(x for x, _ in pts))//2,
      (min(y for _, y in pts)+max(y for _, y in pts))//2)
PY
)
click_at "$mx" "$my"
wait_client "menu clicked"

echo "OK: StatusNotifier icon, DBusMenu and activation"
