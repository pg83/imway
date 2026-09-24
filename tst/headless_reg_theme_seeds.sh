#!/usr/bin/env bash
# The theme's key colors re-theme the desktop live: a near-black neutral
# darkens the desktop behind the windows (its channels fall into the
# linear toe of sRGB), setting the same color again changes nothing, and
# the default neutral brings the old desktop back. A new selection color
# re-themes without taking the desktop's lightness along.
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
mean_now() {
    screenshot "$XDG_RUNTIME_DIR/now.ppm" && patch_mean "$XDG_RUNTIME_DIR/now.ppm"
}

base=$(mean_now)

acked_twice() { [[ "$(grep -c "control: set appearance.neutral" "$IMWAY_LOG" || true)" -ge 2 ]]; }

ctl "set appearance.neutral 0.02,0.02,0.02"
await 100 in_log "control: set appearance.neutral" || { echo "the neutral color is not reachable"; exit 1; }
darker() { [[ "$(mean_now)" -lt $((base - 4)) ]]; }
await 30 darker || { echo "a near-black neutral did not darken the desktop: base=$base now=$(mean_now)"; exit 1; }
dark=$(mean_now)

# the same color again: nothing to re-theme
ctl "set appearance.neutral 0.02,0.02,0.02"
await 100 acked_twice || { echo "the second neutral was not taken"; exit 1; }
[[ "$(mean_now)" -le $((dark + 2)) ]] || { echo "setting the same neutral changed the desktop"; exit 1; }

ctl "set appearance.selection 0.9,0.3,0.1"
await 100 in_log "control: set appearance.selection" || { echo "the selection color is not reachable"; exit 1; }
[[ "$(mean_now)" -le $((dark + 2)) ]] || { echo "a new selection color lifted the dark desktop"; exit 1; }

ctl "set appearance.neutral 0.14,0.14,0.14"
back() { local m; m=$(mean_now); [[ "$m" -ge $((base - 2)) && "$m" -le $((base + 2)) ]]; }
await 30 back || { echo "the default neutral did not bring the desktop back: base=$base now=$(mean_now)"; exit 1; }

expect_alive "re-theming took the compositor with it"
echo "OK: the key colors re-theme the desktop live"
