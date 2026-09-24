#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_INTERNAL=1 IMWAY_SYSFS_BACKLIGHT=./backlight
# imway-pre: mkdir -p backlight/imway0
# imway-pre: printf 'firmware\n' > backlight/imway0/type; printf '255\n' > backlight/imway0/max_brightness; printf '128\n' > backlight/imway0/brightness
# The display page of a panel with a backlight has a brightness row, and
# its slider drives the backlight through sysfs: a click three quarters
# along sets about three quarters of the panel's range.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "imway: backlight ./backlight/imway0, max 255" || { echo "the backlight was not picked"; cat "$IMWAY_LOG"; exit 1; }
raw() { cat "$XDG_RUNTIME_DIR/backlight/imway0/brightness"; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)

# the brightness row follows ui scale, connector and mode: the fourth row
# of the display page, its slider across 366..750
around() { (( $(raw) >= 175 && $(raw) <= 210 )); }
for _ in 1 2 3; do
    click_at $((wx + 366 + (750 - 366) * 3 / 4)) $((wy + 121))
    await 30 around && break
done
around || { echo "the brightness slider did not set the backlight (raw $(raw))"; exit 1; }

expect_alive "compositor died driving the backlight from settings"
echo "OK: the display page's brightness slider drives the backlight"
