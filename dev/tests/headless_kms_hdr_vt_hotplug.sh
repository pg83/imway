#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto --hdr 300
# A hotplug arrives while the vt is switched away: without drm master every
# atomic ioctl (test commits included) bounces with EACCES. That says
# nothing about the display — the HDR configuration must survive untouched
# and come back with the session, not degrade to SDR for good.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "HDR output: BT.2020 + PQ" || { echo "no HDR boot"; cat "$IMWAY_LOG"; exit 1; }
in_log "kms output" || { echo "session did not light up"; cat "$IMWAY_LOG"; exit 1; }

ctl "session 0"
await 50 in_log "session disabled (vt switch away)" || { echo "session did not disable"; exit 1; }

# no drm master: every atomic ioctl fails, TEST_ONLY ones too
ctl "kms-fail-commit 13 1000 1"

# the display is replugged while we are away
ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }
ctl "kms-connector 1"
sleep 0.5

# EACCES is transient, not a rejected configuration — HDR must not degrade
! in_log "falling back to SDR" || {
    echo "transient commit failure degraded HDR to SDR"; cat "$IMWAY_LOG"; exit 1; }

# vt comeback: master returns, the remodeset must relight in HDR
ctl "kms-fail-commit 0 0 0"
ctl "session 1"
await 50 in_log "session enabled, remodeset" || {
    echo "no remodeset on comeback"; cat "$IMWAY_LOG"; exit 1; }

hdr() { dump_field '^hdr' metadata; }
[[ "$(hdr)" == "1" ]] || { echo "HDR did not survive the vt-away hotplug"; dump_state; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after comeback"; exit 1; }

expect_alive "compositor died across a vt-away hotplug"
echo "OK: a hotplug while the vt is away leaves HDR intact"
