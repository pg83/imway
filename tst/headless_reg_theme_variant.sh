#!/usr/bin/env bash
# The light appearance variant inverts the neutral base, so the desktop
# behind the windows gets brighter; the session keeps drawing either way.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# mean brightness of a patch of empty desktop, away from the bar and dock
patch_mean() { # <ppm>
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w * h * 3)
xs = range(700, 1200, 7)
ys = range(400, 750, 7)
vals = [sum(d[((y * w + x) * 3):((y * w + x) * 3 + 3)]) / 3 for y in ys for x in xs]
print(round(sum(vals) / len(vals)))
PY
}

screenshot "$XDG_RUNTIME_DIR/dark.ppm"
dark=$(patch_mean "$XDG_RUNTIME_DIR/dark.ppm")

ctl "set appearance.variant 1" # light
await 20 in_log "control: set appearance.variant" || { echo "the variant is not reachable"; exit 1; }

brighter() {
    local light
    screenshot "$XDG_RUNTIME_DIR/light.ppm" || return 1
    light=$(patch_mean "$XDG_RUNTIME_DIR/light.ppm")
    [[ "$light" -gt $((dark + 60)) ]]
}

await 30 brighter || {
    echo "the light variant did not brighten the desktop: dark=$dark light=$(patch_mean "$XDG_RUNTIME_DIR/light.ppm")"
    exit 1
}

# and back: the dark variant returns the darker base
ctl "set appearance.variant 0"

darker() {
    local back
    screenshot "$XDG_RUNTIME_DIR/back.ppm" || return 1
    back=$(patch_mean "$XDG_RUNTIME_DIR/back.ppm")
    [[ "$back" -lt $((dark + 20)) ]]
}

await 30 darker || { echo "the dark variant did not come back"; exit 1; }

expect_alive "the theme switch took the compositor with it"
echo "OK: the light variant brightens the desktop base and the dark one restores it"
