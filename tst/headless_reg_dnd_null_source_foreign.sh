#!/usr/bin/env bash
# A source-less drag stays inside the client that started it: carried over
# the empty desktop and over another client's window it leaves the
# dragging client's surface and enters nothing, carried back it enters
# again, and the release drops there.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

other_log="$XDG_RUNTIME_DIR/other.log"
"$IMWAY_TESTS_BIN/client_input_health_probe" >"$other_log" 2>&1 &
other_pid=$!
await 100 grep -q "input-health ready" "$other_log" || { echo "the other client did not map"; cat "$other_log"; exit 1; }
wait_rect 'title=input-health'

start_client
wait_client "null-source ready"
wait_rect 'title=dnd-null-source-foreign'

# a point on the other client's window outside the dragging client's
rect() { # <pattern> -> x y w h of the surface
    local line
    line=$(dump_state | grep -m1 "$1")
    awk '{ for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] } print v["imgx"], v["imgy"], v["client_w"], v["client_h"] }' <<<"$line"
}
read -r rx ry rw rh <<<"$(rect 'title=dnd-null-source-foreign')"
read -r gx gy gw gh <<<"$(rect 'title=input-health')"
foreign=""
for p in "$((gx + 8)) $((gy + 8))" "$((gx + gw - 8)) $((gy + 8))" "$((gx + 8)) $((gy + gh - 8))" "$((gx + gw - 8)) $((gy + gh - 8))"; do
    read -r px py <<<"$p"
    if (( px < rx || px >= rx + rw || py < ry || py >= ry + rh )); then
        foreign="$p"
        break
    fi
done
[[ -n "$foreign" ]] || { echo "the other client's window is fully covered"; dump_state; exit 1; }

carry() { # <x> <y>: move, let a frame compute hover, nudge
    ctl "motion $1 $2"
    screenshot "$XDG_RUNTIME_DIR/_c.ppm"
    ctl "motion $(($1 + 1)) $2"
    screenshot "$XDG_RUNTIME_DIR/_c.ppm"
}

cx=$((rx + rw / 2)); cy=$((ry + rh / 2))
carry "$cx" "$cy"
ctl "button left press"
wait_client "null-source started"
carry $((cx + 5)) $((cy + 5))
wait_client "null-source entered 1"

carry 1270 790
wait_client "null-source left 1"
carry "$cx" "$cy"
wait_client "null-source entered 2"

carry $foreign
wait_client "null-source left 2"
grep -q "entered 3" "$CLIENT_LOG" && { echo "the drag entered the dragging client over another client's window"; exit 1; }
carry "$cx" "$cy"
wait_client "null-source entered 3"
ctl "button left release"

expect_client_ok "the source-less drag did not stay with its client"
kill "$other_pid" 2>/dev/null || true
wait "$other_pid" 2>/dev/null || true
expect_alive "a source-less drag across windows killed the compositor"
input_health_probe
echo "OK: a source-less drag enters only its own client's surfaces"
