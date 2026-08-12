#!/usr/bin/env bash
# Per-window keyboard layout with the default us,ru + grp:caps_toggle: Caps
# in window A switches to RU; focusing B restores EN; focusing A restores RU.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

layout_is() { # <EN|RU>
    [[ $(dump_state | awk '/^layout/{print $2}') == "$1" ]]
}

# scene->focusedToplevel is imgui's truth, written a frame after the click
focused_is() { # <app_id>
    [[ $(dump_field "app_id=$1" focused) == 1 ]]
}

start_client
wait_client "two mapped"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

# B cascades over A, so click via exact dump coords: A's top-left corner is
# clear of B, and B is on top so any point of it works
ax=$(dump_field 'app_id=layA' imgx); ay=$(dump_field 'app_id=layA' imgy)
bx=$(dump_field 'app_id=layB' imgx); by=$(dump_field 'app_id=layB' imgy)

# focus A (red), switch it to RU
click_at $((ax + 10)) $((ay + 10))
await 20 focused_is layA || { echo "A did not take focus"; exit 1; }
await 20 layout_is EN || { echo "expected EN after focusing A, got $(dump_state | awk '/^layout/{print $2}')"; exit 1; }
ctl "key 58 press"; ctl "key 58 release"   # Caps: grp:caps_toggle
await 20 layout_is RU || { echo "Caps did not switch to RU"; exit 1; }

# focus B (blue): its own group is still EN. B's bottom-right corner stays
# clear of A even after A got raised by the previous click
click_at $((bx + 290)) $((by + 190))
await 20 focused_is layB || { echo "B did not take focus"; exit 1; }
await 20 layout_is EN || { echo "focusing B did not restore EN"; exit 1; }

# back to A: RU must come back with the focus
click_at $((ax + 10)) $((ay + 10))
await 20 layout_is RU || { echo "focusing A did not restore RU"; exit 1; }

echo "OK: each window kept its own xkb group across focus changes"
