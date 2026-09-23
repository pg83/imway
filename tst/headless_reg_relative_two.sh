#!/usr/bin/env bash
# Two clients with relative pointers: relative motion goes to the client
# under the pointer alone, and follows the pointer to the other one.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

log_a="$XDG_RUNTIME_DIR/a.log"
log_b="$XDG_RUNTIME_DIR/b.log"
"$IMWAY_CLIENT" rel-a FFFF0000 >"$log_a" 2>&1 &
pid_a=$!
await 100 grep -q ready "$log_a" || { echo "client a did not map"; cat "$log_a"; exit 1; }
"$IMWAY_CLIENT" rel-b FF00FF00 >"$log_b" 2>&1 &
pid_b=$!
await 100 grep -q ready "$log_b" || { echo "client b did not map"; cat "$log_b"; exit 1; }
wait_rect 'title=rel-a'
wait_placed 'title=rel-a' || { echo "the window never settled: title=rel-a"; exit 1; }
wait_rect 'title=rel-b'
wait_placed 'title=rel-b' || { echo "the window never settled: title=rel-b"; exit 1; }

rect() { # <pattern> -> x y w h
    dump_state | grep -m1 "$1" | awk '{ for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] } print v["imgx"], v["imgy"], v["client_w"], v["client_h"] }'
}
read -r ax ay aw ah <<<"$(rect 'title=rel-a')"
read -r bx by bw bh <<<"$(rect 'title=rel-b')"
count() { grep -c "^rel$" "$1" || true; }
got_more() { (( $(count "$1") > $2 )); }

# b opened down and right of a: a's top-left corner is a's alone, b's
# bottom-right corner b's
push_on() { # <x> <y> <log>: aim, then push until that client moves
    local n i
    for i in $(seq 20); do
        ctl "motion $1 $2"
        screenshot "$XDG_RUNTIME_DIR/_r.ppm"
        n=$(count "$3")
        ctl "relmotion 1 0"
        await 10 got_more "$3" "$n" && return 0
    done
    return 1
}

b_before=$(count "$log_b")
push_on $((ax + 10)) $((ay + 10)) "$log_a" || { echo "relative motion never reached client a"; cat "$log_a"; exit 1; }
[[ "$(count "$log_b")" == "$b_before" ]] || { echo "client b got relative motion aimed at a"; cat "$log_b"; exit 1; }

a_before=$(count "$log_a")
push_on $((bx + bw - 10)) $((by + bh - 10)) "$log_b" || { echo "relative motion never reached client b"; cat "$log_b"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_r.ppm"
a_after=$(count "$log_a")
n=$(count "$log_b")
ctl "relmotion 1 0"
await 30 got_more "$log_b" "$n" || { echo "client b stopped getting relative motion"; exit 1; }
[[ "$(count "$log_a")" == "$a_after" ]] || { echo "client a got relative motion aimed at b"; cat "$log_a"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-exit"
wait "$pid_a" || { echo "client a failed"; cat "$log_a"; exit 1; }
wait "$pid_b" || { echo "client b failed"; cat "$log_b"; exit 1; }
expect_alive "compositor died routing relative motion between clients"
echo "OK: relative motion goes to the client under the pointer alone"
