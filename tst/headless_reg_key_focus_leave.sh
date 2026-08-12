#!/usr/bin/env bash
# A key held across a click-focus switch: A gets its leave, B's enter
# carries the held key, and the physical release lands on B.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

ax=$(dump_field 'app_id=kfA' imgx); ay=$(dump_field 'app_id=kfA' imgy)
bx=$(dump_field 'app_id=kfB' imgx); by=$(dump_field 'app_id=kfB' imgy)

# focus A (top-left corner stays clear of B) and hold KEY_A
click_at $((ax + 10)) $((ay + 10))
ctl "key 30 press"
wait_client "held on A"

# focus B (its bottom-right corner stays clear of A even raised)
click_at $((bx + 290)) $((by + 190))
wait_client "enter carried the held key"

ctl "key 30 release"
expect_client_ok "held key broke across the focus switch"
echo "OK: leave/enter pair carried the held key across the focus switch"
