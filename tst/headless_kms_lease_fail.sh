#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# A lease request the kernel side cannot satisfy ends in `finished`, never
# in a half-built lease: the connector unplugged after the offer, its
# encoder gone, every crtc it can reach already driving the desktop, and
# the kernel refusing the lease itself. Healed, the same connector leases
# out with its crtc and that crtc's own primary plane.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "offered"

for kind in 1 2 3 4; do
    ctl "kms-lease-fault $kind"
    # the dump is the barrier: the fault is armed once it arrives
    dump_state >/dev/null
    touch "$XDG_RUNTIME_DIR/go-$kind"
    wait_client "refused $kind"
done

ctl "kms-lease-fault 0"
dump_state >/dev/null
touch "$XDG_RUNTIME_DIR/go-0"
wait_client "leased"
expect_client_ok "the lease client failed"
in_log "fake-kms: lease 301 303 304$" || { echo "the lease is not connector, crtc and plane"; grep "fake-kms: lease" "$IMWAY_LOG"; exit 1; }
[[ "$(grep -c "fake-kms: lease" "$IMWAY_LOG")" -eq 1 ]] || { echo "a refused request still reached the kernel"; grep "fake-kms: lease" "$IMWAY_LOG"; exit 1; }

# the desktop pipe never noticed
flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "the desktop pipe stopped flipping"; exit 1; }

expect_alive "compositor died on a refused lease"
echo "OK: every broken lease path finishes the request, the healed one leases"
