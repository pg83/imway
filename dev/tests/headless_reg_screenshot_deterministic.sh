#!/usr/bin/env bash
# Two screenshots of the same idle desktop must match pixel-for-pixel:
# without a real display there is nothing for temporal dither to serve, and
# a per-frame noise shift makes every exact pixel assertion in the suite
# frame-dependent. The top bar is excluded — its clock/cpu/mem widgets are
# legitimately live.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

sleep 0.5
screenshot "$XDG_RUNTIME_DIR/a.ppm"

# force fresh frames between the shots: the desktop content is unchanged
# (no cursor sprite on headless), only the frame counter advances
for i in 1 2 3 4 5; do
    ctl "motion $((600 + i)) 400"
    sleep 0.05
done

sleep 0.3
screenshot "$XDG_RUNTIME_DIR/b.ppm"

python3 - "$XDG_RUNTIME_DIR/a.ppm" "$XDG_RUNTIME_DIR/b.ppm" <<'PY'
import sys
def load(p):
    f = open(p, 'rb'); assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split()); assert f.readline().strip() == b'255'
    return w, h, f.read(w * h * 3)
w, h, a = load(sys.argv[1]); w2, h2, b = load(sys.argv[2])
assert (w, h) == (w2, h2)
top = 60  # below the live menu bar widgets
differ = sum(a[(y * w) * 3:(y * w + w) * 3] != b[(y * w) * 3:(y * w + w) * 3]
             for y in range(top, h))
assert differ == 0, f'idle desktop screenshots differ in {differ} rows: temporal dither leaked into headless'
PY

echo "OK: headless screenshots are frame-independent"
