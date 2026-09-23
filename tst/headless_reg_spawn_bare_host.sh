#!/usr/bin/env bash
# The compositor's children on a bare, old host (see the client): started
# with no PATH in its environment, holding a descriptor its starter forgot
# to close, on a kernel without close_range. The child's command is still
# found on the standard path, and the fallback that closes descriptors one
# by one keeps the stray one out of it: the spawn probe of headless_reg_spawn
# sees only stdio. A second compositor runs the case, in a runtime dir of
# its own so the probe's display name is the one it expects.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "SKIP: no close_range filter in this build"; exit 127; }

bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
probe="$IMWAY_TESTS_BIN/client_reg_spawn"
rt="$XDG_RUNTIME_DIR/bare"
mkdir -p "$rt"

env -u PATH XDG_RUNTIME_DIR="$rt" \
    "$IMWAY_CLIENT" "$bin" --device headless --socket imway-test -- sh -c "PATH=/usr/bin:/bin exec '$probe' boot" \
    >"$rt/out.log" 2>&1 &
bare=$!

fail() {
    echo "$1"
    cat "$rt/out.log" 2>/dev/null || true
    kill "$bare" 2>/dev/null || true
    exit 1
}

await 200 test -s "$rt/spawn-result-boot" || fail "the child of a PATH-less compositor did not start"
[[ "$(cat "$rt/spawn-result-boot")" == ok ]] || fail "the child on a bare host: $(cat "$rt/spawn-result-boot")"
# the stray descriptor was there to leak: the compositor holds it
[[ "$(readlink "/proc/$bare/fd/7")" == /dev/null ]] || fail "the stray descriptor never reached the compositor"

kill -TERM "$bare"
wait "$bare" || fail "the bare-host compositor did not exit cleanly"
grep -q "clean exit" "$rt/out.log" || fail "the bare-host compositor did not exit cleanly"
expect_alive "the scenario's own compositor died"
echo "OK: a PATH-less compositor on a kernel without close_range hands its child only stdio"
