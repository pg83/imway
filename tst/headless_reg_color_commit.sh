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

snap() {
    sleep 0.3
    screenshot "$XDG_RUNTIME_DIR/$1.ppm"
    raw_pixels "$XDG_RUNTIME_DIR/$1.ppm"
}

start_client
wait_client "color-commit: raw"
raw=$(snap raw)
[[ "$raw" -gt 5000 ]] || { echo "raw surface missing: $raw"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-set"
wait_client "color-commit: pending-set"
pending_set=$(snap pending-set)
[[ "$pending_set" -gt 5000 ]] || { echo "set applied before commit: $pending_set"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-commit-set"
wait_client "color-commit: managed"
managed=$(snap managed)
[[ "$managed" -lt 500 ]] || { echo "set did not apply on commit: $managed"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-unset"
wait_client "color-commit: pending-unset"
pending_unset=$(snap pending-unset)
[[ "$pending_unset" -lt 500 ]] || { echo "unset applied before commit: $pending_unset"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-commit-unset"
wait_client "color-commit: unset"
unset=$(snap unset)
[[ "$unset" -gt 5000 ]] || { echo "unset did not restore raw path: $unset"; exit 1; }

echo "OK: color state follows commit (raw=$raw pending-set=$pending_set managed=$managed pending-unset=$pending_unset unset=$unset)"
