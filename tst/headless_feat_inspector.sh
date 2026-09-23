#!/usr/bin/env bash
# Inspector: Super+F12 toggles the inspector overlay; a double click on its
# title bar collapses it to the bar, and another opens it again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

screenshot "$XDG_RUNTIME_DIR/base.ppm"

ctl "key 125 press"  # Super
ctl "key 88 press"   # F12
ctl "key 88 release"
ctl "key 125 release"

changed=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/after.ppm"
    changed=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/after.ppm" 200 100 1080 700)
    [[ "$changed" -gt 2000 ]] && break
done
echo "changed=$changed"
[[ "$changed" -gt 2000 ]] || { echo "inspector overlay did not appear"; exit 1; }
await_imgui inspector || { echo "the inspector is not in the dump"; dump_state; exit 1; }
wx=$(dump_field '^imgui name=inspector ' x); wy=$(dump_field '^imgui name=inspector ' y)
open_h=$(dump_field '^imgui name=inspector ' h)
double_click_title() {
    ctl "motion $((wx + 60)) $((wy + 8))"
    screenshot "$XDG_RUNTIME_DIR/_t.ppm"
    ctl "motion $((wx + 61)) $((wy + 8))"
    screenshot "$XDG_RUNTIME_DIR/_t.ppm"
    ctl "button left press"; ctl "button left release"
    ctl "button left press"; ctl "button left release"
}
collapsed() { (( $(dump_field '^imgui name=inspector ' h) < 40 )); }
expanded() { (( $(dump_field '^imgui name=inspector ' h) == open_h )); }
for _ in 1 2 3; do
    double_click_title
    await 20 collapsed && break
done
collapsed || { echo "a double click on the title bar did not collapse the inspector"; dump_state; exit 1; }
for _ in 1 2 3; do
    double_click_title
    await 20 expanded && break
done
expanded || { echo "a second double click did not open the inspector again"; dump_state; exit 1; }
echo "OK: Super+F12 toggled the inspector overlay"
