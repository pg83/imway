#!/usr/bin/env bash
# Supervisor owns spawning, strips inherited fds, supplies compositor env,
# forgets exited children and tears down the shared process group on SIGTERM.
# imway-args: -- sh -c 'exec "$IMWAY_CLIENT"'
# expect-compositor-exit
set -euo pipefail
. "$(dirname "$0")/lib.sh"

supervisor_child_reaped() {
    local child state count=0

    for child in $(cat "/proc/$IMWAY_PID/task/$IMWAY_PID/children" 2>/dev/null); do
        ((count += 1))
        state=$(awk '{print $3}' "/proc/$child/stat" 2>/dev/null || true)
        [[ "$state" != Z ]] || return 1
    done

    # The compositor is the supervisor's one permanent direct child.
    [[ $count -eq 1 ]]
}

await 100 test -s "$XDG_RUNTIME_DIR/supervisor-result" || {
    echo "supervisor probe did not start"
    exit 1
}

[[ "$(cat "$XDG_RUNTIME_DIR/supervisor-result")" == ok ]] || {
    cat "$XDG_RUNTIME_DIR/supervisor-result"
    exit 1
}

await 100 supervisor_child_reaped || {
    echo "exited startup child was not automatically reaped"
    exit 1
}

expect_alive "compositor followed an ordinary spawned child"

kill -TERM "$IMWAY_PID"

supervisor_gone() {
    ! kill -0 "$IMWAY_PID" 2>/dev/null ||
        [[ "$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null || true)" == Z ]]
}

await 100 supervisor_gone || {
    echo "supervisor stayed alive after composer exit"
    exit 1
}

await 100 grep -q killed "$XDG_RUNTIME_DIR/supervisor-killed" || {
    echo "supervisor did not terminate its process group"
    exit 1
}

echo "OK: supervisor owns spawn, fd isolation and group cleanup"
