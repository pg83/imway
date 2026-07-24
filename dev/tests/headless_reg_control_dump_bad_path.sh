#!/usr/bin/env bash
# The control FIFO is external input: a dump with a missing or unwritable
# path must log and carry on, not take the compositor down on a failed
# rename.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# no path at all: the tmp file opens as ".tmp" in the cwd, the rename target
# is empty — before the fix this tripped STD_VERIFY and killed the session
ctl "dump"
sleep 0.5
expect_alive "compositor died on a pathless dump"

# unwritable directory: open of the tmp file fails
ctl "dump /nonexistent/dir/state"
await 50 in_log "dump: cannot open" || { echo "no diagnostic for the unwritable path"; exit 1; }
expect_alive "compositor died on an unwritable dump path"

# the control channel still works end to end
dump_state | grep -q 'focus id=' || { echo "dump no longer works after bad paths"; exit 1; }

echo "OK: bad dump paths are survivable and diagnosed"
