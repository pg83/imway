#!/usr/bin/env bash
# The evdev directory as the libinput source finds it at boot: only the
# eventN entries that are there are handed to libinput (a file that is no
# device among them is tried once and refused), other names never are, and
# a directory that does not exist leaves the session without input devices
# rather than without a session.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

dir="$XDG_RUNTIME_DIR/staged-input"
mkdir -p "$dir"
touch "$dir/event7" "$dir/mouse0"

kms_boot IMWAY_INPUT_DIR="$dir" --
boot_rc 0 "staged input directory"
boot_has "libinput: client bug: Invalid path $dir/event7$" "staged input directory"
[[ "$(grep -c "Invalid path" <<<"$BOOT_OUT")" -eq 1 ]] || { echo "boot probed more than the one eventN there is"; echo "$BOOT_OUT"; exit 1; }
boot_has "libinput ready, 0 devices" "staged input directory"

kms_boot IMWAY_INPUT_DIR="$XDG_RUNTIME_DIR/no-such-input" --
boot_rc 0 "missing input directory"
boot_has "libinput ready, 0 devices" "missing input directory"
boot_lacks "Invalid path" "missing input directory"

expect_alive "the scenario's own compositor died"
echo "OK: boot hands libinput exactly the eventN nodes that are there"
