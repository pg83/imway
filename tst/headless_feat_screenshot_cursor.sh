#!/usr/bin/env bash
# The screenshot editor asks for the pointer shape its widgets want: Ctrl
# and a click on the zoom slider turn it into a text field, and the pointer
# over that field becomes a text beam in the compositor's scene (the editor
# names it through cursor-shape-v1). A letter key the editor has no use for
# travels its keyboard path and does not close it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set applications.screenshot_action 0"
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }
ctl "key 99 press"; ctl "key 99 release" # Print
viewer_up() { [[ -n "$(dump_field 'title=imway screenshot' id)" ]]; }
await 150 viewer_up || { echo "the editor did not open"; cat "$IMWAY_LOG"; exit 1; }
wait_rect 'title=imway screenshot'

shape() { dump_field '^cursor ' shape; }
text_beam() { [[ "$(shape)" == 9 ]]; } # CursorKind::text

# a letter, then the pointer on the zoom slider: the second row of the left
# panel, under its caption
ctl "key 16 press"; ctl "key 16 release" # q
beam=0
for _ in 1 2 3; do
    vx=$(dump_field 'title=imway screenshot' imgx); vy=$(dump_field 'title=imway screenshot' imgy)
    sx=$((vx + 100)); sy=$((vy + 26))
    ctl "motion $sx $sy"
    screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
    ctl "motion $((sx + 1)) $sy"
    screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
    # the editor learns its modifiers from key events: a letter pressed
    # with Ctrl held carries it to ImGui before the click
    ctl "key 29 press"
    ctl "key 16 press"; ctl "key 16 release"
    ctl "button left press"; ctl "button left release"
    ctl "key 29 release"
    ctl "motion $((sx + 2)) $sy"
    if await 30 text_beam; then
        beam=1
        break
    fi
done
(( beam )) || { echo "the pointer over the slider's text field is shape $(shape), not the text beam"; dump_state; exit 1; }
viewer_up || { echo "the letter key or the click closed the editor"; exit 1; }

# away from the field the editor asks for the arrow again
ctl "motion $((vx + 600)) $((vy + 200))"
arrow() { [[ "$(shape)" != 9 ]]; }
await 30 arrow || { echo "the text beam stayed off the field"; exit 1; }

# the editor leaves on its own: one killed with the compositor at teardown
# never gets to say what it did. The first Escape may only end the zoom
# field's editing, the next one closes the editor
viewer_gone() { [[ -z "$(dump_field 'title=imway screenshot' id)" ]]; }
escape_closes() {
    ctl "key 1 press"; ctl "key 1 release" # Escape
    await 20 viewer_gone
}
await 5 escape_closes || { echo "Escape did not close the editor"; exit 1; }

expect_alive "compositor died following the editor's pointer shapes"
echo "OK: the editor's text field gets the text beam, the arrow elsewhere"
