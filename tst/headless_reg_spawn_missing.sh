#!/usr/bin/env bash
# A `-- CMD` that is nowhere on PATH is refused before fork with a log line,
# a launcher command whose program is missing exits 127 through sh, and a
# launcher command under a replaced WAYLAND_DISPLAY still lands on this
# compositor.
# imway-args: -- imway-no-such-command --flag
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 50 in_log "spawn: imway-no-such-command not found" || { echo "the missing command was not refused"; cat "$IMWAY_LOG"; exit 1; }

launch() { # <command typed into the launcher>
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release"
    await_typing '##launcher' || { echo "the launcher never took text"; dump_state; exit 1; }
    ctl "type $1"
    sleep 0.3
    ctl "key 28 press"; ctl "key 28 release"
}

launch "imway-no-such-command"
await 100 in_log "exited with status 127" || { echo "sh did not report the missing program"; cat "$IMWAY_LOG"; exit 1; }

# the child's environment carries the compositor's socket over an inherited
# WAYLAND_DISPLAY: the probe prints what it sees
cat > "$XDG_RUNTIME_DIR/env" <<SCRIPT
#!/bin/sh
echo "display=\$WAYLAND_DISPLAY" > env.out
SCRIPT
chmod +x "$XDG_RUNTIME_DIR/env"
launch "./env"
await 100 test -s "$XDG_RUNTIME_DIR/env.out" || { echo "the env probe did not run"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(cat "$XDG_RUNTIME_DIR/env.out")" == "display=imway-test" ]] || { echo "unexpected child environment: $(cat "$XDG_RUNTIME_DIR/env.out")"; exit 1; }

expect_alive "compositor died spawning missing commands"
echo "OK: missing commands are refused or fail through sh, the environment reaches the child"
