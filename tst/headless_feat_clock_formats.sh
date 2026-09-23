#!/usr/bin/env bash
# The top bar clock in each of its formats, locale and plain: seconds
# lengthen it, a 12 hour clock adds its AM/PM and so does too, and without
# the date it is shorter than with. The clock sits at the bar's right end
# and the layout indicator left of it, so the leftmost ink on the bar's
# right half moves left as the clock grows.
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

fmt() { # <locale> <date> <24h> <seconds>
    local want="control: set desktop.clock_seconds" before
    before=$(grep -c "$want" "$IMWAY_LOG" || true)
    ctl "set desktop.clock_locale $1"
    ctl "set desktop.clock_date $2"
    ctl "set desktop.clock_24_hour $3"
    ctl "set desktop.clock_seconds $4"
    applied() { (( $(grep -c "$want" "$IMWAY_LOG" || true) > before )); }
    await 20 applied || { echo "the clock settings did not land" >&2; exit 1; }
    # two equal readings in a row: the bar has drawn the new format
    local a b i
    for i in $(seq 20); do
        a=$(left_ink); b=$(left_ink)
        [[ "$a" == "$b" ]] && { echo "$a"; return 0; }
    done
    echo "the bar did not settle" >&2
    exit 1
}

wider() { # <name> <a> <b>: clock a is wider than clock b
    (( $2 < $3 )) || { echo "$1 (left edge $2 vs $3)"; exit 1; }
}

for locale in true false; do
    d24=$(fmt $locale true true false)
    d24s=$(fmt $locale true true true)
    d12=$(fmt $locale true false false)
    d12s=$(fmt $locale true false true)
    n24=$(fmt $locale false true false)
    n24s=$(fmt $locale false true true)
    n12=$(fmt $locale false false false)
    n12s=$(fmt $locale false false true)
    echo "locale=$locale: $d24 $d24s $d12 $d12s $n24 $n24s $n12 $n12s"
    wider "seconds do not lengthen the dated 24 hour clock" "$d24s" "$d24"
    wider "seconds do not lengthen the dated 12 hour clock" "$d12s" "$d12"
    wider "seconds do not lengthen the 24 hour clock" "$n24s" "$n24"
    wider "seconds do not lengthen the 12 hour clock" "$n12s" "$n12"
    wider "AM/PM does not lengthen the dated clock" "$d12" "$d24"
    wider "AM/PM does not lengthen the clock" "$n12" "$n24"
    wider "the date does not lengthen the clock" "$d24" "$n24"
    wider "the date does not lengthen the 12 hour clock with seconds" "$d12s" "$n12s"
done

expect_alive "compositor died drawing the clock formats"
echo "OK: every clock format draws, seconds, AM/PM and the date each lengthen it"
