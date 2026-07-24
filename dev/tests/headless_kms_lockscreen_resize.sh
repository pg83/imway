#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FORCE_CURSOR=1
# imway-args: --device auto
# The lock filter owns output-sized images. A live mode change while locked
# must rebuild them before recording the next filtered frame.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "lockscreen ready"
wait_client "phase 1"
screenshot "$XDG_RUNTIME_DIR/base.ppm"

ctl "key 125 press"
ctl "key 38 press"
ctl "key 38 release"
ctl "key 125 release"

locked=0
for _ in $(seq 1 30); do
    sleep 0.15
    screenshot "$XDG_RUNTIME_DIR/locked.ppm"
    locked=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/locked.ppm" 360 220 920 560)
    [[ "$locked" -gt 10000 ]] && break
done
[[ "$locked" -gt 10000 ]] || { echo "lockscreen did not appear ($locked)"; exit 1; }

ctl "kms-modes 1"
ctl "kms-connector 1"
await 100 in_log "kms output: 1920x1080@60" || {
    echo "mode did not change while locked"
    cat "$IMWAY_LOG"
    exit 1
}

sleep 0.3
screenshot "$XDG_RUNTIME_DIR/locked-resized.ppm"
read -r width height < <(sed -n '2p' "$XDG_RUNTIME_DIR/locked-resized.ppm")
[[ "$width" == 1920 && "$height" == 1080 ]] || {
    echo "lockscreen frame kept the old size: ${width}x${height}"
    exit 1
}
expect_alive "lock filter died while rebuilding for the new mode"

for _ in 1 2 3; do
    ctl "key 45 press"
    ctl "key 45 release"
done
ctl "key 28 press"
ctl "key 28 release"
await 50 in_log "lockscreen closed" || { echo "resized lockscreen did not unlock"; exit 1; }

ctl "key 66 press"
ctl "key 66 release"
expect_client_ok "input was not restored after resized lockscreen"
expect_alive "resized lockscreen teardown killed compositor"
echo "OK: lockscreen rebuilt its filter across a live mode change"
