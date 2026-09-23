#!/usr/bin/env bash
# Two clients with keyboards: the modifiers and keys go to the focused
# client's keyboard only, and when the launcher takes the keyboard with a
# key held, the focused client alone gets that key's release when it is
# let go.
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
read -r ax ay aw ah < <(rect 'title=kbd-a')
read -r bx by bw bh < <(rect 'title=kbd-b')
spot=""
for p in "$((ax + 8)) $((ay + 8))" "$((ax + aw - 8)) $((ay + 8))" "$((ax + 8)) $((ay + ah - 8))" "$((ax + aw - 8)) $((ay + ah - 8))"; do
    read -r px py <<<"$p"
    if (( px < bx || px >= bx + bw || py < by || py >= by + bh )); then
        spot="$p"
        break
    fi
done
[[ -n "$spot" ]] || { echo "client a's window is fully covered"; dump_state; exit 1; }

a_focused() { [[ "$(dump_field 'title=kbd-a' focused)" == 1 ]]; }
for _ in 1 2 3 4 5; do
    click_at $spot
    await 20 a_focused && break
done
a_focused || { echo "client a never took the focus"; dump_state; exit 1; }

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

touch "$XDG_RUNTIME_DIR/go-exit"
wait "$pid_a" || { echo "client a failed"; cat "$log_a"; exit 1; }
wait "$pid_b" || { echo "client b failed"; cat "$log_b"; exit 1; }
expect_alive "compositor died routing keys between two keyboards"
echo "OK: keys and modifiers reach the focused client's keyboard only"
