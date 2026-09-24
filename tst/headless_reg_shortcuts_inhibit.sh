#!/usr/bin/env bash
# While a focused window inhibits them, compositor chords are the window's.
# #14: after the last shortcuts-inhibiting window closes, compositor chords
# must come back to life. With no window focused, Super+F2 must still open the
# launcher — observed as a change in the center of the frame.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "inhibitor active"

# while the focused window inhibits them, the launcher chord is the window's
ctl "key 125 press"  # KEY_LEFTMETA
ctl "key 60 press"   # KEY_F2
ctl "key 60 release"
ctl "key 125 release"
wait_client "inhibited chord reached the window"
compose_frame
compose_frame
[[ -z "$(dump_field 'imgui name=##launcher' x)" ]] || { echo "the launcher opened on an inhibited chord"; dump_state; exit 1; }

wait_client "inhibitor window closed"
sleep 0.3

screenshot "$XDG_RUNTIME_DIR/before.ppm"

# Super+F2 — the launcher chord, gated by shortcutsInhibited
ctl "key 125 press"  # KEY_LEFTMETA
ctl "key 60 press"   # KEY_F2
ctl "key 60 release"
ctl "key 125 release"

# poll: the launcher needs a frame to draw; count changed pixels center-top
changed=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/after.ppm"
    changed=$(region_diff "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" 400 180 880 460)
    [[ "$changed" -gt 800 ]] && break
done
echo "center-region changed pixels: $changed"
# the launcher frame plus the query row alone clear this; the exact count
# scales with how many .desktop entries the environment ships, so the
# threshold must not assume a populated applications directory
[[ "$changed" -gt 800 ]] || {
    echo "launcher did not open — shortcutsInhibited stayed stuck true"; exit 1; }
echo "OK: shortcuts revived — Super+F2 opened the launcher after the inhibitor closed"
