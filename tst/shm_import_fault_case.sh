# A sealed wl_shm pool on a zero-copy backend the scenario forces
# (IMWAY_SHM_BACKEND), whose import the device refuses at one step
# (IMWAY_CHAOS=client-import=K). The failed step reports itself when it has
# a report ($fault_log, empty when it has none), the backend's gate closes
# for the whole device exactly once ($gate_log), and the CPU copy carries
# both of the client's commits to the screen.
# $natural_log names a refusal the local device makes on its own before the
# faulted step is reached; seeing it means this host cannot run the case.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_external_host"

if in_log "wl_shm gates image=0 buffer=0 host=0"; then
    echo "SKIP: this device offers no wl_shm import path"
    exit 127
fi

start_client
wait_client "second sealed buffer committed"

settled() {
    [[ $(grep -c "wl_shm backend cpu" "$IMWAY_LOG") -ge 2 ]]
}

await 100 settled || {
    echo "the CPU copy did not carry both sealed commits"
    cat "$IMWAY_LOG"
    exit 1
}

if [[ -n "${natural_log:-}" ]] && in_log "$natural_log"; then
    echo "SKIP: this device refuses the import before the faulted step"
    exit 127
fi

if ! in_log "$gate_log"; then
    echo "SKIP: the pool never reached the import (no usable /dev/udmabuf)"
    exit 127
fi

if [[ -n "$fault_log" ]] && ! in_log "$fault_log"; then
    echo "the refused step did not report: $fault_log"
    cat "$IMWAY_LOG"
    exit 1
fi

[[ $(grep -c "$gate_log" "$IMWAY_LOG") -eq 1 ]] || {
    echo "the refused import was retried after its gate closed"
    cat "$IMWAY_LOG"
    exit 1
}

point_at_color 32 192 96 || {
    echo "the CPU fallback did not put the buffer on screen"
    exit 1
}

expect_alive "compositor died on a refused wl_shm import"
echo "OK: a refused wl_shm import closes its gate and falls back to the CPU copy"
