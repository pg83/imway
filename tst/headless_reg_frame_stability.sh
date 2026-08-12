#!/usr/bin/env bash
# Two frames of the same idle desktop may differ only by the dither: at most
# one code per channel, everywhere below the live menu-bar widgets. Anything
# larger is real frame-to-frame nondeterminism in the compositor.
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
worst = max(abs(a[i] - b[i]) for i in range(top * w * 3, h * w * 3))
assert worst <= 1, f'frames differ by {worst} codes: more than dither'
PY

echo "OK: idle frames differ by dither only"
