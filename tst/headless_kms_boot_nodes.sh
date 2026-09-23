#!/usr/bin/env bash
# The card node a KMS session opens when no device is named, from a staged
# node directory: a node that is no drm device refuses the atomic
# capability and is closed, missing nodes are skipped, and a scan that runs
# out ends the boot with the directory named. --list reports the same node
# as unusable. The emulator takes the node behind its card from the same
# directory: with none there it fails the boot, with a render node there it
# boots on it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
nodes="$XDG_RUNTIME_DIR/staged-dri"
empty="$XDG_RUNTIME_DIR/empty-dri"
mkdir -p "$nodes" "$empty"
ln -s /dev/null "$nodes/card0"

# the real backend, no emulator in front of it
BOOT_RC=0
BOOT_OUT=$(env -u IMWAY_FAKE_KMS IMWAY_DRI_DIR="$nodes" IMWAY_SETTINGS=advanced.seat_backend=2 timeout 60 "$bin" --device auto --socket imway-boot --frames 3 2>&1) || BOOT_RC=$?
boot_rc 1 "no atomic node"
boot_has "kms: no device with atomic support under $nodes" "no atomic node"
boot_lacks "imway: device " "no atomic node"

rc=0
out=$(env IMWAY_DRI_DIR="$nodes" "$bin" --list 2>&1) || rc=$?
[[ "$rc" -eq 0 ]] || { echo "--list exited $rc: $out"; exit 1; }
grep -qF "$nodes/card0: ?, NO atomic (unusable)" <<<"$out" || { echo "--list did not report the staged node as unusable: $out"; exit 1; }

kms_boot IMWAY_DRI_DIR="$empty" --
boot_rc 1 "emulator without a node"
boot_has "kms: fake device" "emulator without a node"

node=
for n in /dev/dri/renderD* /dev/dri/card*; do
    if [[ -e "$n" ]]; then
        node=$n
        break
    fi
done
if [[ -n "$node" ]]; then
    ln -s "$node" "$empty/renderD128"
    kms_boot IMWAY_DRI_DIR="$empty" --
    boot_rc 0 "emulator on a staged render node"
    boot_has "imway: device fake-kms" "emulator on a staged render node"
    boot_has "clean exit after" "emulator on a staged render node"
fi

expect_alive "the scenario's own compositor died"
echo "OK: the node scan skips what it cannot use and says where it looked"
