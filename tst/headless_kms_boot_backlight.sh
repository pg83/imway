#!/usr/bin/env bash
# The internal panel's backlight as sysfs presents it: a platform device
# beats a raw one, and a class directory that is missing, empty, or holds
# only devices without a readable type or a usable maximum leaves the panel
# without hardware brightness instead of driving a half-described device.
# An HDR panel pins its backlight at full, and a device whose brightness
# file cannot be opened is skipped quietly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bl="$XDG_RUNTIME_DIR/bl"
mkdir -p "$bl/empty" "$bl/notype/dev0" "$bl/nomax/dev0" "$bl/zero/dev0" "$bl/ranked/raw0" "$bl/ranked/plat0" "$bl/pinned/dev0"
printf 'raw\n' > "$bl/nomax/dev0/type"
printf 'raw\n' > "$bl/zero/dev0/type"; printf '0\n' > "$bl/zero/dev0/max_brightness"
printf 'raw\n' > "$bl/ranked/raw0/type"; printf '100\n' > "$bl/ranked/raw0/max_brightness"; printf '10\n' > "$bl/ranked/raw0/brightness"
printf 'platform\n' > "$bl/ranked/plat0/type"; printf '200\n' > "$bl/ranked/plat0/max_brightness"; printf '20\n' > "$bl/ranked/plat0/brightness"
# no brightness file: the HDR pin has nothing to write to
printf 'firmware\n' > "$bl/pinned/dev0/type"; printf '50\n' > "$bl/pinned/dev0/max_brightness"

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

for broken in missing empty notype nomax zero; do
    panel "$bl/$broken"
    boot_rc 0 "$broken backlight"
    boot_lacks "imway: backlight " "$broken backlight"
done

panel "$bl/ranked"
boot_rc 0 "ranked backlight"
boot_has "imway: backlight $bl/ranked/plat0, max 200" "ranked backlight"

panel "$bl/pinned" --hdr 300
boot_rc 0 "hdr panel"
boot_has "imway: backlight $bl/pinned/dev0, max 50" "hdr panel"
boot_has "HDR pins hardware brightness to full" "hdr panel"
[[ ! -e "$bl/pinned/dev0/brightness" ]] || { echo "the pin created a brightness file"; exit 1; }

expect_alive "the scenario's own compositor died"
echo "OK: only a fully described backlight drives the panel"
