#!/usr/bin/env bash
# Alt+Tab: with two toplevels, the switcher overlay appears while Alt is held.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "two toplevels mapped"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/base.ppm"

# hold Alt, tap Tab → the switcher overlay shows
ctl "key 56 press"   # KEY_LEFTALT
ctl "key 15 press"   # KEY_TAB
ctl "key 15 release"

changed=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/overlay.ppm"
    changed=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/overlay.ppm" 200 100 1080 700)
    [[ "$changed" -gt 2000 ]] && break
done
echo "overlay changed=$changed"
[[ "$changed" -gt 2000 ]] || { echo "alt-tab overlay did not appear"; exit 1; }

# Escape drops the switcher without switching. The overlay holds the
# keyboard while it is up, so the dump says when it went away — the client's
# windows keep repainting, which makes a pixel comparison against the
# earlier shot meaningless here.
switcher_holds_keyboard() {
    [[ "$(dump_field '^captured ' kb)" = 1 ]]
}
switcher_gone() {
    [[ "$(dump_field '^captured ' kb)" = 0 ]]
}

await 30 switcher_holds_keyboard || { echo "the switcher does not hold the keyboard"; dump_state; exit 1; }
ctl "key 1 press"; ctl "key 1 release" # KEY_ESC
await 30 switcher_gone || { echo "Escape did not drop the switcher"; dump_state; exit 1; }
ctl "key 56 release" # Alt up: nothing left to commit
expect_alive "the switcher took the compositor with it"
echo "OK: Alt+Tab raises the window switcher overlay, Escape drops it"
