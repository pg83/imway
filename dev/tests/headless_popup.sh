#!/usr/bin/env bash
# Popups: yellow grab popup over a red toplevel, placed by the positioner;
# click outside → popup_done → popup disappears.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

CLIENT_LOG="$XDG_RUNTIME_DIR/client.log"

"$IMWAY_CLIENT" >"$CLIENT_LOG" 2>&1 &

await 50 grep -q "ready for grab" "$CLIENT_LOG" || {
    echo "toplevel did not become ready"; cat "$IMWAY_LOG" "$CLIENT_LOG"; exit 1; }

# Create a real implicit pointer grab and keep the button held until the
# xdg_popup.grab request has been processed.
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/base.ppm"
read -r GRAB_X GRAB_Y < <(python3 - "$XDG_RUNTIME_DIR/base.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
f.readline()
data = f.read(w*h*3)
pts = [(x, y) for y in range(h) for x in range(w)
       if data[(y*w+x)*3] > 200 and data[(y*w+x)*3+1] < 80 and data[(y*w+x)*3+2] < 80]
assert pts, 'red toplevel not visible'
print((min(x for x, _ in pts) + max(x for x, _ in pts)) // 2,
      (min(y for _, y in pts) + max(y for _, y in pts)) // 2)
PY
)
ctl "motion $GRAB_X $GRAB_Y"
sleep 0.2
ctl "button left press"

await 50 in_log "popup mapped" || {
    echo "popup did not map"; cat "$IMWAY_LOG" "$CLIENT_LOG"; exit 1; }
ctl "button left release"
sleep 0.5

screenshot "$XDG_RUNTIME_DIR/with.ppm"

# click into an empty corner → dismiss
ctl "motion 1100 700"
sleep 0.2
ctl "button left press"
sleep 0.2
ctl "button left release"
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/without.ppm"

await 50 grep -q "popup done" "$CLIENT_LOG" || {
    echo "client did not receive popup_done"; cat "$CLIENT_LOG"; exit 1; }

python3 - "$XDG_RUNTIME_DIR/with.ppm" "$XDG_RUNTIME_DIR/without.ppm" <<'PY'
import sys
def counts(path):
    f = open(path, 'rb')
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    data = f.read(w * h * 3)
    red = yellow = 0
    for i in range(0, len(data), 3):
        r, g, b = data[i], data[i+1], data[i+2]
        if r > 200 and g < 80 and b < 80: red += 1
        if r > 200 and g > 200 and b < 80: yellow += 1
    return red, yellow
r1, y1 = counts(sys.argv[1])
r2, y2 = counts(sys.argv[2])
print(f"with popup: red={r1} yellow={y1}; after dismiss: red={r2} yellow={y2}")
assert y1 > 8000, "popup not visible (expected ~10800 yellow)"
assert r1 > 30000, "toplevel not visible"
assert y2 < 100, "popup did not disappear after click outside"
PY
echo "OK: popup placed by positioner and dismissed by click outside"
