#!/usr/bin/env bash
# The focused window's wl_surface dies before its role: a keyboard bound
# afterwards gets no enter for it, and the next window takes the focus
# with no leave for the dead one; both keyboards enter the new window.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "focus mishandled a destroyed focused surface"; exit 1; }
expect_alive "compositor died when the focused surface went first"
input_health_probe
echo "OK: focus moves past a focused window whose surface was destroyed"
