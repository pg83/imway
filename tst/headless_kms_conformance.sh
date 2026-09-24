#!/usr/bin/env bash
# The KMS emulator refuses what the kernel refuses: requests the backend
# never sends (objects that do not exist, rooms too small for a list, a
# property on an object it does not belong to, an ioctl the emulator does
# not model), each through the backend's own fd and held against the
# kernel's answer. The session goes on flipping after them.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-conformance"
await 100 in_log "control: kms conformance" || { echo "the conformance run did not report"; cat "$IMWAY_LOG"; exit 1; }
in_log "control: kms conformance, 0 failed" || { echo "the emulator answered unlike the kernel:"; grep "fake-kms: conformance" "$IMWAY_LOG"; exit 1; }
in_log "fake-kms: unhandled drm ioctl nr" || { echo "the unmodelled ioctl was not named"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "motion 300 300"
await 100 advanced || { echo "flips stopped after the conformance run"; exit 1; }

expect_alive "compositor died under the emulator's conformance run"
echo "OK: the KMS emulator answers malformed requests the way the kernel does"
