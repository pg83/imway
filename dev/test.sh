#!/usr/bin/env bash
# Runs the integration tests. Builds nothing — dev/build.sh builds both the
# compositor and the test clients.
#
# Each test gets a fresh headless compositor; the test script receives its
# whereabouts through the environment (see dev/tests/lib.sh for the list)
# and only drives the scenario. Afterwards the compositor is asked to quit,
# and a compositor that died or hung fails the test regardless of what the
# scenario said.
set -euo pipefail
cd "$(dirname "$0")/.."

B=${B:-build-boot}
IMWAY=${IMWAY:-$B/imway}   # override to test another binary (e.g. a sanitizer build)

[[ -x "$IMWAY" ]] || { echo "no $IMWAY — run dev/build.sh first"; exit 1; }

fail=0

for t in dev/tests/headless_*.sh; do
    name=$(basename "$t" .sh)
    RT=$(mktemp -d)
    log="$RT/imway.log"

    XDG_RUNTIME_DIR="$RT" "$IMWAY" --device headless --socket imway-test --control "$RT/ctl" >"$log" 2>&1 &
    pid=$!

    # ready = socket bound, control FIFO open, and the init-complete marker
    # logged (it prints right before the event loop starts). The scenario can
    # assume a fully-up compositor and never has to poll for startup itself.
    up=""

    for _ in $(seq 1 100); do
        if [[ -S "$RT/imway-test" && -p "$RT/ctl" ]] && grep -q "control FIFO:" "$log" 2>/dev/null; then
            up=1
            break
        fi
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done

    if [[ -z "$up" ]]; then
        echo "FAIL $name: compositor did not come up"
        tail -5 "$log"
        kill "$pid" 2>/dev/null || true
        rm -rf "$RT"
        fail=1
        continue
    fi

    client="$B/tests/${name/headless_/client_}"
    [[ -x "$client" ]] || client=""

    rc=0
    XDG_RUNTIME_DIR="$RT" WAYLAND_DISPLAY=imway-test \
        IMWAY_CTL="$RT/ctl" IMWAY_LOG="$log" IMWAY_PID="$pid" \
        IMWAY_CLIENT="$client" IMWAY_CLIENT_LOG="$RT/client.log" bash "$t" || rc=$?

    # clean shutdown is part of every test: quit over the FIFO, bounded wait
    died=""
    kill -0 "$pid" 2>/dev/null || died=1
    [[ -z "$died" ]] && { timeout 2 sh -c "echo quit > '$RT/ctl'" 2>/dev/null || true; }

    hung=""

    for _ in $(seq 1 50); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done

    kill -0 "$pid" 2>/dev/null && { hung=1; kill -9 "$pid" 2>/dev/null || true; }

    crc=0
    wait "$pid" 2>/dev/null || crc=$?

    if [[ $rc -eq 127 ]]; then
        echo "SKIP $name"
    elif [[ $rc -ne 0 ]]; then
        echo "FAIL $name (rc=$rc)"
        tail -10 "$log"
        fail=1
    elif [[ -n "$died" || -n "$hung" || $crc -ne 0 ]]; then
        echo "FAIL $name: compositor ${hung:+hung}${died:+died mid-test} (rc=$crc)"
        tail -10 "$log"
        fail=1
    else
        echo "PASS $name"
    fi

    rm -rf "$RT"
done

exit $fail
