# The lock screen's blur filter builds its GPU objects on the first frame
# after the lock. The scenario's IMWAY_CHAOS fails one of those Vulkan calls:
# a device that cannot build the filter ends the session — no recovery —
# and the half-built filter tears down with the rest of the compositor
# without faulting on the handles it never got.
ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
exec 3>&-

compositor_gone() {
    # the harness has not reaped the compositor yet, so kill -0 would still
    # succeed on the zombie — read the real process state instead
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}

await 100 compositor_gone || {
    echo "the compositor outlived a filter it could not build"
    cat "$IMWAY_LOG"
    exit 1
}

in_log "imway: fatal" || {
    echo "the failure was not reported"
    cat "$IMWAY_LOG"
    exit 1
}

! in_log "fatal signal" || {
    echo "the half-built filter faulted on teardown"
    cat "$IMWAY_LOG"
    exit 1
}

echo "OK: a Vulkan failure building the lock filter ends the session cleanly"
