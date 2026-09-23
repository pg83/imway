#!/usr/bin/env bash
# imway-env: IMWAY_SYSFS_POWER_SUPPLY=./power
# imway-pre: mkdir -p power/BAT0 && printf 'Battery\n' > power/BAT0/type && printf '55\n' > power/BAT0/capacity && printf 'Discharging\n' > power/BAT0/status
# The top bar's battery reading and its setting: shown while discharging by
# default, gone with "never", back with "always" even while charging, and
# gone again under "when discharging" once it charges. The reading sits left
# of the clock, so the leftmost ink on the bar's right half says whether it
# is drawn.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# leftmost column in the right half of the bar with text on it: well apart
# from the bar's colour, which dithering alone moves by a step or two
left_ink() {
    screenshot "$XDG_RUNTIME_DIR/_bar.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/_bar.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); px = f.read(w * h * 3)
bg = px[(10 * w + w // 2) * 3:(10 * w + w // 2) * 3 + 3]
def ink(x, y):
    p = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
    return sum(abs(p[i] - bg[i]) for i in range(3)) > 120
for x in range(w // 2, w):
    if any(ink(x, y) for y in range(3, 19)):
        print(x)
        sys.exit(0)
print(w)
PY
}

reads() { [[ "$(dump_field '^battery ' pct)" == "$1" && "$(dump_field '^battery ' discharging)" == "$2" ]]; }
await 100 reads 55 1 || { echo "the battery was not read: $(dump_state | grep '^battery ')"; exit 1; }

settled() { # prints the bar's left edge once two readings agree
    local a b i
    for i in $(seq 20); do
        a=$(left_ink); b=$(left_ink)
        [[ "$a" == "$b" ]] && { echo "$a"; return 0; }
    done
    return 1
}
setting() { # <value>
    local before
    before=$(grep -c "control: set desktop.battery" "$IMWAY_LOG" || true)
    ctl "set desktop.battery $1"
    landed() { (( $(grep -c "control: set desktop.battery" "$IMWAY_LOG" || true) > before )); }
    await 20 landed || { echo "the battery setting did not land"; exit 1; }
}

shown=$(settled)
setting 0 # never
hidden=$(settled)
(( shown < hidden )) || { echo "\"never\" left the reading on the bar ($shown vs $hidden)"; exit 1; }

printf 'Charging\n' > power/BAT0/status
await 100 reads 55 0 || { echo "charging was not read"; exit 1; }
setting 2 # always
always=$(settled)
(( always < hidden )) || { echo "\"always\" did not show a charging battery ($always vs $hidden)"; exit 1; }

setting 1 # when discharging
charging=$(settled)
(( charging == hidden )) || { echo "\"when discharging\" shows a charging battery ($charging vs $hidden)"; exit 1; }

expect_alive "compositor died switching the battery display"
echo "OK: the battery reading follows its setting and the charge state"
