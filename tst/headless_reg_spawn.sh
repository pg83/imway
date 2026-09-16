#!/usr/bin/env bash
# The compositor starts and reaps its own children. A probe spawned through
# `-- CMD` at boot and another launched from the launcher after the session
# has captured a frame and cycled the lockscreen inherit nothing beyond stdio
# on /dev/null, the environment and the process group; a one-shot command is
# reaped without leaving a zombie; and when the compositor exits its children
# receive SIGTERM.
# imway-args: -- sh -c 'exec "$IMWAY_CLIENT" boot'
# expect-compositor-exit
set -euo pipefail
. "$(dirname "$0")/lib.sh"

result() { # <tag>
    cat "$XDG_RUNTIME_DIR/spawn-result-$1" 2>/dev/null || true
}

probe_ok() { # <tag>
    await 100 test -s "$XDG_RUNTIME_DIR/spawn-result-$1" || { echo "$1 probe did not report"; exit 1; }
    [[ "$(result "$1")" == ok ]] || { echo "$1 probe: $(result "$1")"; cat "$IMWAY_LOG"; exit 1; }
}

launch() { # <command typed into the launcher>
    ctl "key 125 press"  # Super
    ctl "key 60 press"   # F2
    ctl "key 60 release"
    ctl "key 125 release"
    sleep 0.3
    ctl "type $1"
    sleep 0.3
    ctl "key 28 press"; ctl "key 28 release" # Enter: nothing highlighted, the text runs as a command
}

locked() {
    [[ "$(dump_field '^captured ' kb)" = 1 ]]
}

no_zombie() {
    local child state

    for child in $(cat "/proc/$IMWAY_PID/task/$IMWAY_PID/children" 2>/dev/null); do
        state=$(awk '{print $3}' "/proc/$child/stat" 2>/dev/null || true)
        [[ "$state" != Z ]] || return 1
    done
}

compositor_gone() {
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}

probe_ok boot

# work the session first: a frame capture and a lock/unlock cycle create
# the memfds, dma-bufs and sync files a later child must not inherit
screenshot "$XDG_RUNTIME_DIR/shot.ppm"
ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await 100 locked || { echo "lockscreen did not open"; exit 1; }
ctl "type xxx"
sleep 0.4
ctl "key 28 press"; ctl "key 28 release"
await 100 in_log "lockscreen closed" || { echo "lockscreen did not unlock"; exit 1; }

# launcher commands run through sh -c in the compositor's working
# directory, which is this scratch dir
cat > "$XDG_RUNTIME_DIR/late" <<SCRIPT
#!/bin/sh
exec "$IMWAY_CLIENT" late
SCRIPT
chmod +x "$XDG_RUNTIME_DIR/late"
launch "exec ./late"
probe_ok late

launch true
await 100 in_log "exited with status 0" || { echo "one-shot child was not reaped"; cat "$IMWAY_LOG"; exit 1; }
no_zombie || { echo "a zombie child remains"; exit 1; }

kill -TERM "$IMWAY_PID"
await 100 compositor_gone || { echo "compositor did not exit on SIGTERM"; exit 1; }

for tag in boot late; do
    await 100 grep -q killed "$XDG_RUNTIME_DIR/spawn-killed-$tag" || {
        echo "$tag probe outlived the compositor without SIGTERM"
        exit 1
    }
done

echo "OK: children inherit only stdio, env and group, get reaped, and die with the compositor"
