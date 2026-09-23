#!/usr/bin/env bash
# The internal panel's backlight as sysfs presents it: only a fully
# described device drives the panel; anything less leaves it without
# hardware brightness instead of driving a half-described device.
# This one: the kernel's own class (an empty override), a missing class
# directory and an empty one, and a good device behind a connector that
# cannot be read back when the panel looks for its backlight.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bl="$XDG_RUNTIME_DIR/bl"
mkdir -p "$bl/empty"

panel() { # <backlight root> [imway args...]
    local root=$1
    shift
    kms_boot IMWAY_FAKE_KMS_INTERNAL=1 IMWAY_SYSFS_BACKLIGHT="$root" -- "$@"
}

# an empty override is no override: the kernel's own class is read, and
# whatever this host has there, the panel boots
panel ""
boot_rc 0 "the kernel's backlight class"
boot_has "kms output: 1280x800@60"

for broken in missing empty; do
    panel "$bl/$broken"
    boot_rc 0 "$broken backlight"
    boot_lacks "imway: backlight " "$broken backlight"
done

mkdir -p "$bl/good/dev0"
printf 'raw\n' > "$bl/good/dev0/type"
printf '100\n' > "$bl/good/dev0/max_brightness"
printf '50\n' > "$bl/good/dev0/brightness"
panel "$bl/good"
boot_rc 0 "good backlight"
boot_has "imway: backlight $bl/good/dev0, max 100" "good backlight"

# the pipe pick reads the connector first; the backlight's read is next
kms_boot IMWAY_FAKE_KMS_INTERNAL=1 IMWAY_SYSFS_BACKLIGHT="$bl/good" IMWAY_FAKE_KMS_FAIL_LOOKUPS=connector:101:2:1 --
boot_rc 0 "connector unreadable for the backlight"
boot_has "kms output: 1280x800@60" "connector unreadable for the backlight"
boot_lacks "imway: backlight " "connector unreadable for the backlight"

expect_alive "the scenario's own compositor died"
echo "OK: a missing or empty backlight class, or an unreadable connector, leaves the panel alone"
