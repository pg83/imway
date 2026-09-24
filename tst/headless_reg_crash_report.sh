#!/usr/bin/env bash
# A compositor of the test build that takes a fatal signal names it on
# stderr after everything it logged so far, with the faulting address and
# the stack it can walk, and then still dies by that signal, so a crash
# reads as a crash and not as an exit. A second compositor next to the
# scenario's own is the one that crashes, its stderr a file as the
# scenario runner's is.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

# the sanitizers bring their own fault reports; those builds leave the
# handler out
grep -qa "imway: fatal signal " "$bin" || { echo "SKIP: this build has no crash handler (sanitized)"; exit 127; }

out="$XDG_RUNTIME_DIR/crash.log"
"$bin" --device auto --socket imway-crash 2>"$out" &
pid=$!

up() { grep -q "socket imway-crash," "$out"; }
await 300 up || { echo "the second compositor did not come up"; cat "$out"; kill -9 "$pid" 2>/dev/null || true; exit 1; }
first=$(head -1 "$out")

kill -SEGV "$pid"
rc=0
wait "$pid" 2>/dev/null || rc=$?

fail() { echo "$1"; cat "$out"; exit 1; }

[[ "$rc" == $((128 + 11)) ]] || fail "the crashed compositor exited $rc, not by SIGSEGV"
report=$(grep -n "^imway: fatal signal 11 at 0x[0-9a-f]*, stack follows$" "$out" | cut -d: -f1 || true)
[[ -n "$report" ]] || fail "the crash was not reported"
# the report goes after the log, it does not overwrite it
[[ "$(head -1 "$out")" == "$first" ]] || fail "the log's first line was overwritten"
socket=$(grep -n "socket imway-crash," "$out" | cut -d: -f1)
(( report > socket )) || fail "the report does not follow the log"

# a libc without execinfo walks no stack; the build then has no word for it
if grep -qa "no unwind info at the fault" "$bin"; then
    frames=$(tail -n "+$((report + 1))" "$out" | grep -c 'imway_test' || true)
    (( frames >= 1 )) || fail "the report carries no stack"
fi

expect_alive "the scenario's own compositor died"
echo "OK: a fatal signal is reported after the log with its address and stack, and still kills"
