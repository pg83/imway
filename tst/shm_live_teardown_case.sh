# The session ends on an exception while a client still shows a wl_shm
# buffer on the backend the scenario forces (IMWAY_SHM_BACKEND, traced by
# IMWAY_SHM_TRACE as $backend_log): the lock screen's first frame cannot
# build its blur filter (IMWAY_CHAOS=vulkan=0) and the throw leaves the
# event loop. The display still takes its clients down before the
# exception goes on, while the renderer and the rest of the compositor are
# alive: the client's toplevel is destroyed through its hooks (logged
# before the fatal line), the buffer's GPU state goes with it, and nothing
# is left for the renderer's own teardown to free.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_external_host"

# the first sealed buffer is enough: sampled in place (udmabuf-image) it is
# held until something replaces it, so the client would wait forever for
# the release it needs before its second commit
start_client
wait_client "first sealed buffer committed"

await 100 in_log "$backend_log" || {
    if in_log "wl_shm backend"; then
        echo "SKIP: the pool did not reach the forced backend ($backend_log)"
        exit 127
    fi

    echo "the buffer reached no wl_shm backend"
    cat "$IMWAY_LOG"
    exit 1
}

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
    echo "the compositor outlived a lock filter it could not build"
    cat "$IMWAY_LOG"
    exit 1
}

in_log "imway: fatal" || {
    echo "the failure was not reported"
    cat "$IMWAY_LOG"
    exit 1
}

! in_log "fatal signal" || {
    echo "the teardown faulted on the live client's buffer"
    cat "$IMWAY_LOG"
    exit 1
}

destroyed=$(grep -n "toplevel shm-external-host destroyed" "$IMWAY_LOG" | head -1 | cut -d: -f1)
fatal=$(grep -n "imway: fatal" "$IMWAY_LOG" | head -1 | cut -d: -f1)
[[ -n "$destroyed" && "$destroyed" -lt "$fatal" ]] || {
    echo "the live client was not taken down before the exception left the display"
    cat "$IMWAY_LOG"
    exit 1
}

wait "$CLIENT_PID" || true

echo "OK: an exception out of the event loop still takes the clients down first"
