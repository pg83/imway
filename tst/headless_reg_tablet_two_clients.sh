#!/usr/bin/env bash
# The pen between two clients' windows, tip up: from one client's window
# onto its own subsurface the tool leaves one surface and enters the other,
# and onto the other client's window it goes out of proximity for the first
# client, with no lift, and comes in for the second; carried there touching
# the first window, the first client sees the tip lift before the tool goes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_tablet_focus"
log_a="$XDG_RUNTIME_DIR/a.log"
log_b="$XDG_RUNTIME_DIR/b.log"
"$IMWAY_CLIENT" >"$log_a" 2>&1 &
pid_a=$!
await 100 grep -q "tool ready" "$log_a" || { echo "client a did not map"; cat "$log_a"; exit 1; }
"$IMWAY_CLIENT" >"$log_b" 2>&1 &
pid_b=$!
await 100 grep -q "tool ready" "$log_b" || { echo "client b did not map"; cat "$log_b"; exit 1; }

two_placed() { [[ "$(dump_state | grep -c 'imgx=.*app_id=tablet-focus')" == 2 ]]; }
await 100 two_placed || { echo "the two windows were not laid out"; dump_state; exit 1; }
field() { # <nth window> <field>
    dump_state | grep 'app_id=tablet-focus' | sed -n "$1p" | awk -v f="$2" '{ for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == f) print kv[2] }'
}
ax=$(field 1 imgx); ay=$(field 1 imgy); bx=$(field 2 imgx); by=$(field 2 imgy)

# the windows are 300x200 with a subsurface on the right 100; the second
# one opens down and right of the first, so the first keeps a strip along
# its top and the second a strip along its bottom
(( bx > ax && by > ay + 12 && by + 200 > ay + 212 )) || { echo "unexpected placement: $ax,$ay and $bx,$by"; exit 1; }
a_main="$((ax + 8)) $((ay + 6))"
a_sub="$((ax + 250)) $((ay + 6))"
b_main="$((bx + 8)) $((by + 194))"

pen() { # <phase> <x> <y>
    ctl "tablet $*"
    screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
}

pen proximity_in $a_main
pen motion $a_main
await 50 grep -q "^prox_in main 1$" "$log_a" || { echo "the pen did not come in over the first window"; cat "$log_a"; exit 1; }

pen motion $a_sub
pen motion $a_sub
await 50 grep -q "^prox_in sub 2$" "$log_a" || { echo "the pen did not move onto the first window's subsurface"; cat "$log_a"; exit 1; }

pen motion $b_main
pen motion $b_main
await 50 grep -q "^prox_in main 1$" "$log_b" || { echo "the pen did not come in over the second window"; cat "$log_b"; exit 1; }
await 50 grep -q "^prox_out 2$" "$log_a" || { echo "the first client kept the pen"; cat "$log_a"; exit 1; }
grep -q "^up " "$log_a" && { echo "a pen that never touched was lifted"; cat "$log_a"; exit 1; }
grep -q "^prox_in" <(sed -n '/^prox_in main 1$/,$p' "$log_b" | tail -n +2) && { echo "the second client got a second proximity"; cat "$log_b"; exit 1; }

pen proximity_out $b_main
await 50 grep -q "^prox_out 1$" "$log_b" || { echo "the second client kept the pen out of proximity"; cat "$log_b"; exit 1; }

# the tip down on the first window and carried onto the second: the first
# client sees the tip lift before the tool goes
pen proximity_in $a_main
pen motion $a_main
await 50 grep -q "^prox_in main 3$" "$log_a" || { echo "the pen did not come back over the first window"; cat "$log_a"; exit 1; }
pen down $a_main 0.5
await 50 grep -q "^down 1$" "$log_a" || { echo "the tip did not land on the first window"; cat "$log_a"; exit 1; }
pen motion $b_main
pen motion $b_main
await 50 grep -q "^prox_out 3$" "$log_a" || { echo "the first client kept the pen touching it"; cat "$log_a"; exit 1; }
up_line=$(grep -n -x "up 1" "$log_a" | cut -d: -f1)
out_line=$(grep -n -x "prox_out 3" "$log_a" | cut -d: -f1)
[[ -n "$up_line" && "$up_line" -lt "$out_line" ]] || { echo "the tip was not lifted before the tool left"; cat "$log_a"; exit 1; }

kill "$pid_a" "$pid_b" 2>/dev/null || true
wait "$pid_a" "$pid_b" 2>/dev/null || true
expect_alive "compositor died moving the pen between two clients"
echo "OK: the pen hands proximity from one client to the other, tip up"
