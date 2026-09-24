#!/usr/bin/env bash
# What else shows up in the evdev directory: names that are not eventN,
# event numbers past the 64 slots libinput is driven through, and an
# eventN that is no device. None of them becomes an input device or takes
# a slot, removals of any of them are harmless, and a real node plugged
# afterwards into the slot a bogus file tried still gets it; the directory
# itself vanishing ends the watch quietly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "libinput ready, 0 devices" || { echo "the KMS session brought up no libinput source"; cat "$IMWAY_LOG"; exit 1; }

dir="$XDG_RUNTIME_DIR/input"

# the directory is empty at boot: nothing in it was handed to libinput
! grep -q "Invalid path $dir/" "$IMWAY_LOG" || { echo "boot probed evdev nodes that are not there"; grep -c "Invalid path" "$IMWAY_LOG"; exit 1; }
probes() { grep -c "Invalid path $dir/$1\$" "$IMWAY_LOG" || true; }

# await re-runs its command, so the count is read inside a function
probed_since() { [[ "$(probes event5)" -gt "$1" ]]; }

before5=$(probes event5)
touch "$dir/mouse0" "$dir/event99" "$dir/event5"
await 50 probed_since "$before5" || { echo "the plugged event5 was never tried"; cat "$IMWAY_LOG"; exit 1; }

# the watch keeps working: a second try at the same slot is made
again5=$(probes event5)
rm "$dir/event5"
touch "$dir/event5"
await 50 probed_since "$again5" || { echo "the slot was taken by a file that is no device"; cat "$IMWAY_LOG"; exit 1; }

rm "$dir/mouse0" "$dir/event99" "$dir/event5"
dump_state >/dev/null

! in_log "Invalid path $dir/event99" || { echo "a number past the slots was handed to libinput"; exit 1; }
! in_log "Invalid path $dir/mouse0" || { echo "a non-event name was handed to libinput"; exit 1; }
! in_log "input device event" || { echo "a bogus file was taken for a device"; cat "$IMWAY_LOG"; exit 1; }

rmdir "$dir"
dump_state >/dev/null

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "the session stopped after the directory went away"; exit 1; }

expect_alive "compositor died on a strange input directory"
echo "OK: only eventN device nodes within the slots become input devices"
