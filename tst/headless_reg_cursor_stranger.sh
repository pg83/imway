#!/usr/bin/env bash
# A client the pointer has left cannot set the cursor over another client's
# window: its old enter serial is no longer among the enters, so neither
# wl_pointer.set_cursor nor a cursor-shape device takes it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

log_o="$XDG_RUNTIME_DIR/owner.log"
"$IMWAY_CLIENT" owner >"$log_o" 2>&1 &
pid_o=$!
await 100 grep -q "owner ready" "$log_o" || { echo "the owner did not map"; cat "$log_o"; exit 1; }
start_client stranger
wait_client "stranger ready"
wait_rect 'app_id=cursor-owner'
wait_rect 'app_id=cursor-stranger'

rect() { # <pattern> -> x y w h
    dump_state | grep -m1 "$1" | awk '{ for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] } print v["imgx"], v["imgy"], v["client_w"], v["client_h"] }'
}
read -r ox oy ow oh <<<"$(rect 'app_id=cursor-owner')"
read -r sx sy sw sh <<<"$(rect 'app_id=cursor-stranger')"

hover() { # <x> <y>
    ctl "motion $1 $2"
    screenshot "$XDG_RUNTIME_DIR/_c.ppm"
    ctl "motion $(($1 + 1)) $2"
    screenshot "$XDG_RUNTIME_DIR/_c.ppm"
}

# the stranger opened second, down and right of the owner: its bottom-right
# corner is its own, the owner's top-left corner the owner's
hover $((sx + sw - 10)) $((sy + sh - 10))
wait_client "entered"
hover $((ox + 10)) $((oy + 10))
wait_client "left"
wait_client "asked"
screenshot "$XDG_RUNTIME_DIR/_c.ppm"
[[ "$(dump_field '^cursor ' surface)" == 0 ]] || { echo "the stranger's set_cursor took"; dump_state; exit 1; }
[[ "$(dump_field '^cursor ' shape)" == 0 ]] || { echo "the stranger's cursor shape took"; dump_state; exit 1; }

touch "$XDG_RUNTIME_DIR/go-exit"
expect_client_ok "the stranger failed"
wait "$pid_o" || { echo "the owner failed"; cat "$log_o"; exit 1; }
expect_alive "compositor died refusing a stranger's cursor"
echo "OK: a client the pointer left cannot set the cursor"
