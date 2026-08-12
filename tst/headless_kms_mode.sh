#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto --mode 1920x1080
# An explicit --mode picks the non-preferred connector mode: the session
# boots at that size and flips.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1920x1080@60" || {
    echo "requested mode not taken"
    cat "$IMWAY_LOG"
    exit 1
}

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips at the explicit mode"; exit 1; }

expect_alive "compositor died at an explicit mode"
echo "OK: explicit mode selected and flipping"
