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

start_client
wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/mapped.ppm"
read -r n low high < <(mapped_surface_codes "$XDG_RUNTIME_DIR/mapped.ppm")

# PQ white is 10,000 nit. A 600-nit target is PQ code 178, allowing a
# two-code tolerance for XR30 and PPM quantization.
[[ "$n" -gt 55000 ]]
[[ "$low" -ge 176 && "$high" -le 180 ]]

echo "OK: 10000-nit PQ highlight maps into 600-nit output"
