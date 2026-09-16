#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600
set -euo pipefail
. "$(dirname "$0")/lib.sh"

mapped_surface_codes() {
    python3 - "$1" <<'PY'
import collections, sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
assert f.readline().strip() == b'255'
d = f.read(w*h*3)
colors = collections.Counter(zip(d[::3], d[1::3], d[2::3]))
mapped = [(rgb, n) for rgb, n in colors.items()
          if 176 <= min(rgb) and max(rgb) <= 180]
print(sum(n for _, n in mapped),
      min(min(rgb) for rgb, _ in mapped),
      max(max(rgb) for rgb, _ in mapped))
PY
}

# PQ white is 10,000 nit. A 600-nit target is PQ code 178, allowing a
# two-code tolerance for XR30 and PPM quantization. Take fresh readbacks
# until the surface is the thing being measured: the frame carrying the
# client's HDR content is a round trip away, and a sanitized build makes
# that window wide. Before it lands there are no mapped codes at all and
# the counter has nothing to report.
MAPPED="?"
mapped_settled() {
    local n low high

    screenshot "$XDG_RUNTIME_DIR/mapped.ppm" || return 1
    read -r n low high < <(mapped_surface_codes "$XDG_RUNTIME_DIR/mapped.ppm" 2>/dev/null) ||
        return 1
    MAPPED="n=$n low=$low high=$high"

    [[ "$n" -gt 55000 && "$low" -ge 176 && "$high" -le 180 ]]
}

start_client
wait_client "managed"
await 40 mapped_settled || {
    echo "the highlight never mapped into the 600-nit window: $MAPPED"; exit 1; }

echo "OK: 10000-nit PQ highlight maps into 600-nit output"
