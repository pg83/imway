#!/usr/bin/env bash
# Exact disjoint confinement plus commit-synchronized set_region.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "region ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
imgx=$(dump_field 'app_id=constraint-region-commit' imgx)
imgy=$(dump_field 'app_id=constraint-region-commit' imgy)

ctl "motion $((imgx + 250)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((imgx + 251)) $((imgy + 100))"
wait_client "region confined"
wait_client "region pending"

# The uncommitted replacement must not apply, and the gap between the two
# initial rectangles must remain outside the effective region.
ctl "relmotion -100 0"
sleep 0.2
ctl "key 2 press"; ctl "key 2 release" # KEY_1
wait_client "region committed"

# Once committed, only the left rectangle remains.
ctl "relmotion -300 0"
sleep 0.2
ctl "key 3 press"; ctl "key 3 release" # KEY_2

expect_client_ok "constraint region transaction/shape is wrong"
echo "OK: constraint regions stay exact and apply on surface commit"
