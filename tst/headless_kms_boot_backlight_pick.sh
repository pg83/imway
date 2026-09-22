#!/usr/bin/env bash
# The internal panel's backlight as sysfs presents it: only a fully
# described device drives the panel; anything less leaves it without
# hardware brightness instead of driving a half-described device.
# This one: a platform device outranks a raw one, and an HDR panel pins
# its backlight at full, skipping quietly a device whose brightness file
# cannot be opened.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bl="$XDG_RUNTIME_DIR/bl"
mkdir -p "$bl/ranked/raw0" "$bl/ranked/plat0" "$bl/pinned/dev0"
printf 'raw\n' > "$bl/ranked/raw0/type"; printf '100\n' > "$bl/ranked/raw0/max_brightness"; printf '10\n' > "$bl/ranked/raw0/brightness"
printf 'platform\n' > "$bl/ranked/plat0/type"; printf '200\n' > "$bl/ranked/plat0/max_brightness"; printf '20\n' > "$bl/ranked/plat0/brightness"
# no brightness file: the HDR pin has nothing to write to
printf 'firmware\n' > "$bl/pinned/dev0/type"; printf '50\n' > "$bl/pinned/dev0/max_brightness"

panel() { # <backlight root> [imway args...]
    local root=$1
    shift
    kms_boot IMWAY_FAKE_KMS_INTERNAL=1 IMWAY_SYSFS_BACKLIGHT="$root" -- "$@"
}

panel "$bl/ranked"
boot_rc 0 "ranked backlight"
boot_has "imway: backlight $bl/ranked/plat0, max 200" "ranked backlight"

panel "$bl/pinned" --hdr 300
boot_rc 0 "hdr panel"
boot_has "imway: backlight $bl/pinned/dev0, max 50" "hdr panel"
boot_has "HDR pins hardware brightness to full" "hdr panel"
[[ ! -e "$bl/pinned/dev0/brightness" ]] || { echo "the pin created a brightness file"; exit 1; }

expect_alive "the scenario's own compositor died"
echo "OK: the best backlight is picked and HDR pins it"
