#!/usr/bin/env bash
# The fixed dock reserves the left work area, keeps minimized applications and
# restores/focuses them through their grouped icon.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "dock client ready"
wait_mapped

field_is() {
    [[ "$(dump_field 'app_id=dock-test' "$1")" = "$2" ]]
}

window_reserved() {
    local x
    x=$(dump_field 'app_id=dock-test' x)
    [[ "$x" =~ ^[0-9]+$ && "$x" -ge 58 ]]
}
await 50 window_reserved || {
    echo "window overlaps reserved dock: x=$(dump_field 'app_id=dock-test' x)"
    exit 1
}

wait_client "minimize requested"
await 50 field_is minimized 1 || {
    echo "client minimize request was not applied"
    exit 1
}

# Applications start at the dock top: the first 48px slot is the app's.
click_at 29 29
await 50 field_is minimized 0 || {
    echo "dock icon did not restore the application"
    exit 1
}
await 50 field_is activated 1 || {
    echo "restored application did not receive focus"
    exit 1
}

# the focused window's slot (always the top one) carries an accent glow:
# orange-tinted pixels around the icon, red channel well above blue
glow_in_top_slot() { # <ppm>
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); data = f.read(w*h*3)
hits = 0
for y in range(0, 58):
    for x in range(0, 58):
        i = (y*w+x)*3
        if data[i] > 60 and data[i] > data[i+2] + 30:
            hits += 1
sys.exit(0 if hits > 40 else 1)
PY
}

screenshot "$XDG_RUNTIME_DIR/focused.ppm"
glow_in_top_slot "$XDG_RUNTIME_DIR/focused.ppm" || {
    echo "no accent glow on the focused dock slot"; exit 1; }

# click the empty desktop: nothing is focused now, and the dock must not
# keep the slot lit off the sticky xdg activated flag
click_at 700 400
screenshot "$XDG_RUNTIME_DIR/before-launcher.ppm"
glow_in_top_slot "$XDG_RUNTIME_DIR/before-launcher.ppm" && {
    echo "dock glow survived losing the focus to the desktop"; exit 1; }

python3 - "$XDG_RUNTIME_DIR/before-launcher.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); data = f.read(w*h*3)
def pixel(x, y):
    i = (y*w+x)*3
    return tuple(data[i:i+3])
def patch(x, y):
    # 5x5 mean: the output is dithered, single pixels wobble one code —
    # averaging keeps the material comparable while a real seam persists
    acc = [0, 0, 0]
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            p = pixel(x + dx, y + dy)
            for c in range(3):
                acc[c] += p[c]
    return tuple(v / 25.0 for v in acc)
# Blank points in the dock, in the top bar, and on both sides of their joint
# must be the same material — no border and no per-window shadow seam. The
# bar sample sits at x=300, right of the focused-app_id text.
samples = [patch(10, 400), patch(300, 10), patch(55, 10), patch(60, 10)]
spread = max(max(s) for s in samples) - min(min(s) for s in samples)
assert spread < 0.6, f'desktop chrome has a seam: {samples}'
assert pixel(57, 400) != samples[0], 'desktop chrome outer border is missing'
assert sum(pixel(65, 400)) < sum(pixel(100, 400)), 'desktop chrome outer shadow is missing'
bright = [(x, y) for y in range(h) for x in range(58)
          if min(pixel(x, y)) > 180]
assert bright and max(y for _, y in bright) > h - 58, 'launcher icon does not own the lower-left corner'
PY
# the launcher is the bottom slot now; its popup grows upward from the anchor
click_at 29 770
screenshot "$XDG_RUNTIME_DIR/launcher.ppm"
launcher_diff=$(region_diff "$XDG_RUNTIME_DIR/before-launcher.ppm" \
    "$XDG_RUNTIME_DIR/launcher.ppm" 58 300 430 795)
[[ "$launcher_diff" -gt 1000 ]] || {
    echo "permanent dock icon did not open an anchored launcher ($launcher_diff)"
    exit 1
}

expect_alive "compositor died handling dock activation"
echo "OK: dock reserves, minimizes, restores and anchors launcher"
