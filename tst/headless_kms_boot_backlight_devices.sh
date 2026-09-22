#!/usr/bin/env bash
# The internal panel's backlight as sysfs presents it: only a fully
# described device drives the panel; anything less leaves it without
# hardware brightness instead of driving a half-described device.
# This one: devices without a readable type, without a maximum, with a
# zero maximum.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bl="$XDG_RUNTIME_DIR/bl"
mkdir -p "$bl/notype/dev0" "$bl/nomax/dev0" "$bl/zero/dev0"
printf 'raw\n' > "$bl/nomax/dev0/type"
printf 'raw\n' > "$bl/zero/dev0/type"; printf '0\n' > "$bl/zero/dev0/max_brightness"

panel() { # <backlight root> [imway args...]
    local root=$1
    shift
    kms_boot IMWAY_FAKE_KMS_INTERNAL=1 IMWAY_SYSFS_BACKLIGHT="$root" -- "$@"
}

for broken in notype nomax zero; do
    panel "$bl/$broken"
    boot_rc 0 "$broken backlight"
    boot_lacks "imway: backlight " "$broken backlight"
done

expect_alive "the scenario's own compositor died"
echo "OK: a half-described backlight device is not driven"
