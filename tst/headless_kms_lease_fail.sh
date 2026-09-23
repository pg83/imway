#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# A lease request the kernel side cannot satisfy ends in `finished`, never
# in a half-built lease: the connector unplugged after the offer, its
# encoder gone, every crtc it can reach already driving the desktop, the
# kernel refusing the lease itself, and the driver failing to list its
# resources. A device bound meanwhile offers nothing. A plane the driver
# cannot describe is left out of the lease rather than failing it, and a
# plane list it cannot read leaves them all out the same way; healed,
# the same connector leases out with its crtc and that crtc's own primary
# plane.
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
ctl "kms-fail-lookup resources"
dump_state >/dev/null
touch "$XDG_RUNTIME_DIR/go-5"
wait_client "refused 5"

ctl "kms-fail-lookup plane:304"
dump_state >/dev/null
touch "$XDG_RUNTIME_DIR/go-6"
wait_client "leased without the plane"
in_log "fake-kms: lease 301 303$" || { echo "the lease without its plane is not connector and crtc"; grep "fake-kms: lease" "$IMWAY_LOG"; exit 1; }

ctl "kms-fail-lookup planes"
dump_state >/dev/null
touch "$XDG_RUNTIME_DIR/go-7"
wait_client "leased without planes"
[[ "$(grep -c "fake-kms: lease 301 303$" "$IMWAY_LOG")" -eq 2 ]] || { echo "the lease without a plane list is not connector and crtc"; grep "fake-kms: lease" "$IMWAY_LOG"; exit 1; }

ctl "kms-fail-lookup "
dump_state >/dev/null
touch "$XDG_RUNTIME_DIR/go-0"
wait_client "^leased$"
expect_client_ok "the lease client failed"
in_log "fake-kms: lease 301 303 304$" || { echo "the lease is not connector, crtc and plane"; grep "fake-kms: lease" "$IMWAY_LOG"; exit 1; }
[[ "$(grep -c "fake-kms: lease" "$IMWAY_LOG")" -eq 3 ]] || { echo "a refused request still reached the kernel"; grep "fake-kms: lease" "$IMWAY_LOG"; exit 1; }

# the desktop pipe never noticed
flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "the desktop pipe stopped flipping"; exit 1; }

expect_alive "compositor died on a refused lease"
echo "OK: every broken lease path finishes the request, the healed one leases"
