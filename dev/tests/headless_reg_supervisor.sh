#!/usr/bin/env bash
# Supervisor owns spawning, strips inherited fds, supplies compositor env and
# tears down the shared process group when the watched startup child exits.
# imway-args: -- sh -c 'exec "$IMWAY_CLIENT"'
# expect-compositor-exit
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 test -s "$XDG_RUNTIME_DIR/supervisor-result" || {
    echo "supervisor probe did not start"
    exit 1
}

[[ "$(cat "$XDG_RUNTIME_DIR/supervisor-result")" == ok ]] || {
    cat "$XDG_RUNTIME_DIR/supervisor-result"
    exit 1
}

touch "$XDG_RUNTIME_DIR/supervisor-release"

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
