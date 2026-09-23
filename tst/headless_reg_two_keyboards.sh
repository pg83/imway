#!/usr/bin/env bash
# Two clients with keyboards: the modifiers and keys go to the focused
# client's keyboard only, and when the launcher takes the keyboard with a
# key held, the focused client alone gets that key's release when it is
# let go. Under a global layout policy the group switched in one window
# stays when the focus moves to the other. Each client's text input, never
# enabled, follows its own client's keyboard focus alone.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

log_a="$XDG_RUNTIME_DIR/a.log"
log_b="$XDG_RUNTIME_DIR/b.log"
"$IMWAY_CLIENT" kbd-a FFFF0000 >"$log_a" 2>&1 &
pid_a=$!
await 100 grep -q ready "$log_a" || { echo "client a did not map"; cat "$log_a"; exit 1; }
"$IMWAY_CLIENT" kbd-b FF00FF00 >"$log_b" 2>&1 &
pid_b=$!
await 100 grep -q ready "$log_b" || { echo "client b did not map"; cat "$log_b"; exit 1; }
wait_rect 'title=kbd-a'
wait_rect 'title=kbd-b'

rect() { # <pattern> -> x y w h of the surface
    dump_state | grep -m1 "$1" | awk '{ for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] } print v["imgx"], v["imgy"], v["client_w"], v["client_h"] }'
}
read -r ax ay aw ah <<<"$(rect 'title=kbd-a')"
read -r bx by bw bh <<<"$(rect 'title=kbd-b')"
# a corner of one window outside the other: either may be raised
uncovered() { # <x y w h of the window> <x y w h of the other>
    local p px py
    for p in "$(($1 + 8)) $(($2 + 8))" "$(($1 + $3 - 8)) $(($2 + 8))" "$(($1 + 8)) $(($2 + $4 - 8))" "$(($1 + $3 - 8)) $(($2 + $4 - 8))"; do
        read -r px py <<<"$p"
        if (( px < $5 || px >= $5 + $7 || py < $6 || py >= $6 + $8 )); then
            echo "$p"
            return 0
        fi
    done
    return 1
}
spot=$(uncovered "$ax" "$ay" "$aw" "$ah" "$bx" "$by" "$bw" "$bh") || { echo "client a's window is fully covered"; dump_state; exit 1; }
spot_b=$(uncovered "$bx" "$by" "$bw" "$bh" "$ax" "$ay" "$aw" "$ah") || { echo "client b's window is fully covered"; dump_state; exit 1; }

a_focused() { [[ "$(dump_field 'title=kbd-a' focused)" == 1 ]]; }
for _ in 1 2 3 4 5; do
    click_at $spot
    await 20 a_focused && break
done
a_focused || { echo "client a never took the focus"; dump_state; exit 1; }
await 50 grep -q "^ti enter$" "$log_a" || { echo "client a's text input did not enter"; cat "$log_a"; exit 1; }

ctl "key 42 press"
await 50 grep -q "^mods 1$" "$log_a" || { echo "client a got no shift"; cat "$log_a"; exit 1; }
ctl "key 42 release"
ctl "key 30 press"
await 50 grep -q "^key 30 pressed$" "$log_a" || { echo "client a got no key press"; cat "$log_a"; exit 1; }

# the launcher takes the keyboard while the key is still held
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_imgui '##launcher' || { echo "the launcher did not open"; dump_state; exit 1; }
ctl "key 30 release"
await 50 grep -q "^key 30 released$" "$log_a" || { echo "client a kept a key the launcher took"; cat "$log_a"; exit 1; }
ctl "key 1 press"; ctl "key 1 release" # Escape
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }

grep -q "^key \|^mods 1$" "$log_b" && { echo "the unfocused client got keyboard events"; cat "$log_b"; exit 1; }

# a global layout policy: the group switched in one window stays when the
# focus moves to the other
ctl "set keyboard.layouts us,ru"
ctl "set keyboard.options grp:alt_shift_toggle"
ctl "set keyboard.layout_policy 0"
await 50 in_log "control: set keyboard.layout_policy" || { echo "settings are not reachable"; exit 1; }
layout_is() { [[ "$(dump_state | awk '/^layout/ { print $2 }')" == "$1" ]]; }
await 50 layout_is EN || { echo "the new layout list did not start on its first group"; dump_state; exit 1; }
ctl "key 56 press"; ctl "key 42 press"; ctl "key 42 release"; ctl "key 56 release" # Alt+Shift
await 50 layout_is RU || { echo "alt+shift did not switch the group"; dump_state; exit 1; }
b_focused() { [[ "$(dump_field 'title=kbd-b' focused)" == 1 ]]; }
for _ in 1 2 3 4 5; do
    click_at $spot_b
    await 20 b_focused && break
done
b_focused || { echo "client b never took the focus"; dump_state; exit 1; }
await 50 grep -q "^ti leave$" "$log_a" || { echo "client a's text input did not leave"; cat "$log_a"; exit 1; }
layout_is RU || { echo "a global layout followed the focus"; dump_state; exit 1; }

touch "$XDG_RUNTIME_DIR/go-exit"
wait "$pid_a" || { echo "client a failed"; cat "$log_a"; exit 1; }
wait "$pid_b" || { echo "client b failed"; cat "$log_b"; exit 1; }
expect_alive "compositor died routing keys between two keyboards"
echo "OK: keys and modifiers reach the focused client's keyboard only"
