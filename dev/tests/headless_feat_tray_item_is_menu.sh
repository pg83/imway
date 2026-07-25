#!/usr/bin/env bash
# private-session-bus
# ItemIsMenu primary-click policy: the default opens DBusMenu, while the
# Desktop settings toggle restores the ordinary StatusNotifier Activate call.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

open_settings() {
    ctl "key 125 press"  # Super
    ctl "key 60 press"   # F2
    ctl "key 60 release"
    ctl "key 125 release"
    sleep 0.2
    ctl "type settings"
    sleep 0.3
    ctl "key 103 press"; ctl "key 103 release" # Up: select the action
    ctl "key 28 press"; ctl "key 28 release"   # Enter
    sleep 0.3
}

start_client
wait_client "registered"
wait_client "layout requested"

point_at_color 255 0 255 || { echo "ItemIsMenu tray pixmap did not appear"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 255)

# Default policy: primary click opens the compositor-rendered menu and must
# not call Activate. Its single row is the changed rectangle to the right.
ctl "motion 500 500"
screenshot "$XDG_RUNTIME_DIR/no-menu.ppm"
click_at "$x" "$y"
screenshot "$XDG_RUNTIME_DIR/menu.ppm"

! grep -q "activated" "$CLIENT_LOG" || {
    echo "ItemIsMenu primary unexpectedly called Activate"
    exit 1
}

read -r mx my < <(python3 - "$XDG_RUNTIME_DIR/no-menu.ppm" "$XDG_RUNTIME_DIR/menu.ppm" <<'PY'
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
assert len(pts) > 100, 'ItemIsMenu primary did not render DBusMenu'
print((min(x for x, _ in pts)+max(x for x, _ in pts))//2,
      (min(y for _, y in pts)+max(y for _, y in pts))//2)
PY
)
click_at "$mx" "$my"
wait_client "menu clicked"

# Disable the policy through its public Settings page.
open_settings
click_at 120 216   # desktop page
click_at 420 118   # "open DBusMenu"
click_at 706 91    # close settings
sleep 0.2

point_at_color 255 0 255 || { echo "tray pixmap disappeared after settings"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 255)
click_at "$x" "$y"
wait_client "activated"

expect_alive "compositor died switching ItemIsMenu primary policy"
echo "OK: ItemIsMenu primary menu and Settings Activate policy"
