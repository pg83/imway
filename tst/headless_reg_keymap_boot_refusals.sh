#!/usr/bin/env bash
# A keyboard that cannot be built at boot: no xkb context, no keymap from
# the configured layouts nor from the defaults, no xkb state for it, or no
# file to hand it to clients through (its memfd, or the write into it).
# Each refuses the session with exit code 1, the failed step on record,
# never a signal.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

refused() { # <chaos> <what the log names> <failed check>
    local rc=0 out

    out=$(IMWAY_CHAOS="$1" timeout 60 "$imway_bin" --device headless --socket imway-keymap --frames 3 2>&1) || rc=$?
    [[ "$rc" -eq 1 ]] || { echo "$1: exit $rc, expected 1"; echo "$out"; exit 1; }
    [[ -z "$2" ]] || grep -qF "imway: keymap unusable: $2" <<<"$out" || { echo "$1: the log does not say '$2'"; echo "$out"; exit 1; }
    grep "imway: fatal" <<<"$out" | grep -q "verify failed: $3$" || { echo "$1: the refusal does not name $3"; echo "$out"; exit 1; }
    ! grep -q "keeping the current keymap\|clean exit after" <<<"$out" || { echo "$1: the boot went on without a keymap"; echo "$out"; exit 1; }
}

refused xkb-context=1 "" ctx
refused xkb-keymap=0 "no keymap compiles" "fd >= 0"
refused xkb-state=0 "no xkb state for it" "fd >= 0"
refused keymap-file=0 "its file cannot be written" "fd >= 0"
refused keymap-file=1 "its file cannot be written" "fd >= 0"

expect_alive "the scenario's own compositor died"
echo "OK: a keyboard that cannot be built refuses the boot cleanly"
