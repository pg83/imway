#!/usr/bin/env bash
# empty input region: the surface is visible but never receives a pointer enter.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
wait_client "ready"

# move the pointer over the red pixels and force frames; no enter must arrive
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
for off in "20 15" "40 30" "60 45"; do
    set -- $off
    ctl "motion $((x+$1)) $((y+$2))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
done

expect_client_ok "pointer entered an input-transparent surface"
echo "OK: empty input region kept the pointer out"
