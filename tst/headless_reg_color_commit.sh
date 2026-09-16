#!/usr/bin/env bash
# Color set/unset is double-buffered and unset restores the raw sRGB path.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

raw_pixels() {
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
print(sum(1 for i in range(0, len(d), 3)
          if abs(d[i]-180) < 20 and abs(d[i+1]-120) < 20 and abs(d[i+2]-60) < 20))
PY
}

snap() { # <name>
    sleep 0.3
    screenshot "$XDG_RUNTIME_DIR/$1.ppm"
    raw_pixels "$XDG_RUNTIME_DIR/$1.ppm"
}

# Take fresh frames until the raw-path pixel count is what the phase expects,
# and echo the count it settled on. A fixed sleep reads whichever frame it
# lands on, and under a sanitizer that is regularly the one before the
# client's commit was composed.
#
# Only the phases that assert a change may wait like this. The two that
# assert the change has NOT happened yet keep snap: polling until the old
# state is on screen is satisfied by the first stale frame, which is exactly
# what those two are meant to catch.
await_snap() { # <name> <cond over $n>
    local i n=0

    for ((i = 0; i < 40; i++)); do
        if screenshot "$XDG_RUNTIME_DIR/$1.ppm"; then
            n=$(raw_pixels "$XDG_RUNTIME_DIR/$1.ppm")

            if eval "[[ $2 ]]"; then
                echo "$n"

                return 0
            fi
        fi

        sleep 0.2
    done

    echo "$n"

    return 1
}

start_client
wait_client "color-commit: raw"
raw=$(await_snap raw '"$n" -gt 5000') || { echo "raw surface missing: $raw"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-set"
wait_client "color-commit: pending-set"
pending_set=$(snap pending-set)
[[ "$pending_set" -gt 5000 ]] || { echo "set applied before commit: $pending_set"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-commit-set"
wait_client "color-commit: managed"
managed=$(await_snap managed '"$n" -lt 500') || { echo "set did not apply on commit: $managed"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-unset"
wait_client "color-commit: pending-unset"
pending_unset=$(snap pending-unset)
[[ "$pending_unset" -lt 500 ]] || { echo "unset applied before commit: $pending_unset"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-commit-unset"
wait_client "color-commit: unset"
unset=$(await_snap unset '"$n" -gt 5000') || { echo "unset did not restore raw path: $unset"; exit 1; }

echo "OK: color state follows commit (raw=$raw pending-set=$pending_set managed=$managed pending-unset=$pending_unset unset=$unset)"
