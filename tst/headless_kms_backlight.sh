#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_INTERNAL=1 IMWAY_SYSFS_BACKLIGHT=./backlight
# imway-args: --device auto
# imway-pre: mkdir -p backlight/raw0 backlight/imway0
# imway-pre: printf 'raw\n' > backlight/raw0/type; printf '100\n' > backlight/raw0/max_brightness; printf '50\n' > backlight/raw0/brightness
# imway-pre: printf 'firmware\n' > backlight/imway0/type; printf '255\n' > backlight/imway0/max_brightness; printf '128\n' > backlight/imway0/brightness
# The backlight of an internal panel: the firmware device wins over the raw
# one, the brightness keys step it through sysfs behind an on-screen display,
# and the step floors at one raw unit instead of switching the panel off.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "imway: backlight ./backlight/imway0, max 255" || {
    echo "the firmware backlight was not picked"
    cat "$IMWAY_LOG"
    exit 1
}

raw() { cat "$XDG_RUNTIME_DIR/backlight/imway0/brightness"; }

[[ "$(raw)" == 128 ]] || { echo "the staged brightness changed at startup: $(raw)"; exit 1; }

# the key steps by the configured 5% of 255, and an OSD comes up
screenshot "$XDG_RUNTIME_DIR/plain.ppm"
ctl "key 225 press"; ctl "key 225 release" # KEY_BRIGHTNESSUP
brighter() { [[ "$(raw)" -gt 128 ]]; }
await 50 brighter || { echo "the brightness key did not reach sysfs: $(raw)"; exit 1; }
up=$(raw)

screenshot "$XDG_RUNTIME_DIR/osd.ppm"
[[ "$(region_diff "$XDG_RUNTIME_DIR/plain.ppm" "$XDG_RUNTIME_DIR/osd.ppm" 0 30 1280 780)" -gt 100 ]] || {
    echo "no on-screen display after the brightness key"; exit 1; }

ctl "key 224 press"; ctl "key 224 release" # KEY_BRIGHTNESSDOWN
dimmer() { [[ "$(raw)" -lt "$up" ]]; }
await 50 dimmer || { echo "the dim key did not reach sysfs: $(raw)"; exit 1; }

# all the way down: the floor is one raw unit, never zero
for _ in $(seq 1 30); do
    ctl "key 224 press"; ctl "key 224 release"
done
floored() { [[ "$(raw)" == 1 ]]; }
await 100 floored || { echo "the brightness floor is $(raw), expected 1"; exit 1; }

# and back up again, so the increment path runs from the floor too
for _ in $(seq 1 5); do
    ctl "key 225 press"; ctl "key 225 release"
done
recovered() { [[ "$(raw)" -gt 1 ]]; }
await 100 recovered || { echo "the brightness did not come back up"; exit 1; }

# the display settings page shows a brightness slider for such a panel
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release"
sleep 0.3
ctl "type settings"
sleep 0.3
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
settings_open() {
    [[ -n "$(dump_field '^imgui name=settings ' x)" ]]
}
await 50 settings_open || { echo "settings did not open"; dump_state; exit 1; }

expect_alive "compositor died driving the backlight"
echo "OK: the firmware backlight steps through sysfs with an OSD and a floor"
