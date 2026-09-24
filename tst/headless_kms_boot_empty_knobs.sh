#!/usr/bin/env bash
# Every path knob of the test build set to the empty string counts as not
# set: the session boots on its defaults (the real sysfs, the default font,
# no autostart file) and a child it spawns logs where it always does.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_AUTOSTART_FILE= IMWAY_SYSFS_POWER_SUPPLY= IMWAY_SYSFS_BACKLIGHT= IMWAY_SYSFS_DRM= \
    IMWAY_SYSTEM_FONT= IMWAY_CHILD_LOG= IMWAY_SYSFS_UDMABUF_LIMIT= IMWAY_TERMINAL= -- -- true
boot_rc 0 "empty knobs"
boot_has "clean exit after" "empty knobs"
boot_has "spawned [0-9]*: .*/true$" "empty knobs"
boot_has "child [0-9]* exited with status 0" "empty knobs"

expect_alive "the scenario's own compositor died"
echo "OK: empty path knobs are no knobs"
