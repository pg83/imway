# Sourced by every headless_*.sh. dev/test.sh provides the environment:
#   XDG_RUNTIME_DIR  per-test scratch dir, removed after the test
#   WAYLAND_DISPLAY  socket of the per-test compositor (already running)
#   IMWAY_CTL        control FIFO of that compositor
#   IMWAY_LOG        compositor log
#   IMWAY_PID        compositor pid
#   IMWAY_CLIENT     the test's client binary, empty if it has none
# Exit 127 = skip. The runner quits the compositor itself and fails the
# test if it died or hung — tests only drive the scenario.

# background clients die with the test
trap 'kill $(jobs -p) 2>/dev/null || true' EXIT

ctl() {
    echo "$1" > "$IMWAY_CTL"
}

in_log() {
    grep -q "$1" "$IMWAY_LOG"
}

# await <tries> <cmd...> — poll at 0.1s until the command succeeds
await() {
    local i

    for ((i = 0; i < $1; i++)); do
        "${@:2}" && return 0
        sleep 0.1
    done

    return 1
}

# request a screenshot and wait until the file settles: it appears at
# open() and fills up afterwards, so mere existence is a truncated read
screenshot() {
    rm -f "$1"
    ctl "screenshot $1"

    local prev=-1 size

    for _ in $(seq 1 100); do
        size=$(stat -c %s "$1" 2>/dev/null || echo -1)
        [[ "$size" -gt 0 && "$size" == "$prev" ]] && return 0
        prev=$size
        sleep 0.1
    done

    echo "screenshot $1 did not settle" >&2

    return 1
}
