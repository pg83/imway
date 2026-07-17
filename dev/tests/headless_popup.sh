#!/usr/bin/env bash
# Popups: yellow grab popup over a red toplevel, placed by the positioner;
# click outside → popup_done → popup disappears.
set -euo pipefail

IMWAY="$1"
CLIENT="$2"

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"

"$IMWAY" --device headless --socket imway-test --control "$RT/ctl" >"$RT/imway.log" 2>&1 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" && -p "$RT/ctl" ]] && break
    sleep 0.1
done
[[ -S "$RT/imway-test" ]] || { echo "socket did not appear"; exit 1; }

WAYLAND_DISPLAY=imway-test "$CLIENT" >"$RT/client.log" 2>&1 &
CLIENT_PID=$!

for _ in $(seq 1 50); do
    grep -q "ready for grab" "$RT/client.log" && break
    sleep 0.1
done
grep -q "ready for grab" "$RT/client.log" || {
    echo "toplevel did not become ready"; cat "$RT/imway.log" "$RT/client.log"; exit 1; }

# Create a real implicit pointer grab and keep the button held until the
# xdg_popup.grab request has been processed.
sleep 0.5
echo "screenshot $RT/base.ppm" > "$RT/ctl"
for _ in $(seq 1 50); do
    [[ -f "$RT/base.ppm" ]] && break
    sleep 0.1
done
read -r GRAB_X GRAB_Y < <(python3 - "$RT/base.ppm" <<'PY'
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
echo "motion $GRAB_X $GRAB_Y" > "$RT/ctl"
sleep 0.2
echo "button left press" > "$RT/ctl"

for _ in $(seq 1 50); do
    grep -q "popup mapped" "$RT/imway.log" && break
    sleep 0.1
done
grep -q "popup mapped" "$RT/imway.log" || {
    echo "popup did not map"; cat "$RT/imway.log" "$RT/client.log"; exit 1; }
echo "button left release" > "$RT/ctl"
sleep 0.5

C() { echo "$1" > "$RT/ctl"; sleep 0.2; }
C "screenshot $RT/with.ppm"

# click into an empty corner → dismiss
C "motion 1100 700"
C "button left press"
C "button left release"
sleep 0.5
C "screenshot $RT/without.ppm"
C "quit"
wait "$IMWAY_PID" 2>/dev/null || true
kill "$CLIENT_PID" 2>/dev/null || true

grep -q "popup done" "$RT/client.log" || {
    echo "client did not receive popup_done"; cat "$RT/client.log"; exit 1; }

python3 - "$RT/with.ppm" "$RT/without.ppm" <<'PY'
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
