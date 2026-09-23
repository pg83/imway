# Sourced by headless_feat_settings_shrunk_*.sh: open the settings dialog,
# switch to the nav entry $SHRUNK_PAGE, then drag the dialog down to its
# smallest size by its grip. The page opens a table of its own, which the
# squeezed-out page must refuse to draw, and the dialog survives it.

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
await_input "settings" || { echo "the field did not take 'settings'"; dump_state; exit 1; }
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "settings did not open"; exit 1; }

win() { dump_field '^imgui name=settings ' "$1"; }

# nav entries are one text line plus item spacing apart under the title bar
# and the window padding
page_changed() {
    screenshot "$XDG_RUNTIME_DIR/_after.ppm"
    (( $(region_diff "$XDG_RUNTIME_DIR/_before.ppm" "$XDG_RUNTIME_DIR/_after.ppm" $(( $(win x) + 160 )) $(( $(win y) + 30 )) $(( $(win x) + $(win w) )) $(( $(win y) + 300 ))) > 300 ))
}
screenshot "$XDG_RUNTIME_DIR/_before.ppm"
click_at $(( $(win x) + 40 )) $(( $(win y) + 36 + SHRUNK_PAGE * 20 ))
await 50 page_changed || { echo "nav entry $SHRUNK_PAGE did not switch the page"; dump_state; exit 1; }

x=$(( $(win x) + $(win w) - 4 )); y=$(( $(win y) + $(win h) - 4 ))
ctl "motion $x $y"
screenshot "$XDG_RUNTIME_DIR/_g.ppm"
ctl "motion $((x + 1)) $y"
screenshot "$XDG_RUNTIME_DIR/_g.ppm"
ctl "button left press"
for s in 1 2 3 4 5; do
    ctl "motion $((x + 1 - 800 * s / 5)) $((y - 560 * s / 5))"
    screenshot "$XDG_RUNTIME_DIR/_g.ppm"
done
ctl "button left release"

tiny() { (( $(win h) <= 60 && $(win w) <= 200 )); }
await 50 tiny || { echo "the dialog did not shrink on page $SHRUNK_PAGE ($(win w)x$(win h))"; dump_state; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_g.ppm" # a frame drawn squeezed
imgui_win settings >/dev/null || { echo "the dialog went away squeezed on page $SHRUNK_PAGE"; exit 1; }
expect_alive "compositor died with the settings page $SHRUNK_PAGE squeezed out"
