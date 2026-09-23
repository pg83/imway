#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_INTERNAL=1 IMWAY_SYSFS_BACKLIGHT=./backlight
# imway-args: --device auto
# imway-pre: mkdir -p backlight/imway0
# imway-pre: printf 'firmware\n' > backlight/imway0/type; printf '255\n' > backlight/imway0/max_brightness; printf '128\n' > backlight/imway0/brightness
# A fullscreen dma-buf client is on the primary plane when compositor ui
# comes up without a composed frame to build it: a toast, the launcher
# asked for by its chord, the brightness OSD, the lock screen. Each must
# take the buffer off the plane so the ui is drawn at all, and once it is
# gone again the buffer must go back.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_scanout_taint"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "set notifications.timeout 3"
ctl "set display.osd_seconds 2"

start_client
wait_client "taint candidate mapped"

tlid=$(dump_field 'title=kms-taint' id)
on_plane() { [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]; }
off_plane() { [[ "$(dump_field '^scanout' candidate)" == 0 ]]; }
state() { dump_state | grep -E '^(scanout|notifications|captured)' | tr '\n' ' '; }

await 100 on_plane || { echo "the fullscreen dma-buf never went to the plane"; dump_state; exit 1; }

# a toast posted while the buffer is on the plane
ctl "notify app 0 0 over-the-plane"
toast_up() { [[ "$(dump_field '^notifications' active)" == 1 ]]; }
await 50 toast_up || { echo "the notification was not posted: $(state)"; exit 1; }
await 50 off_plane || { echo "the toast is up but the buffer stayed on the plane: $(state)"; exit 1; }
await 100 on_plane || { echo "the buffer did not go back after the toast expired: $(state)"; exit 1; }

# the launcher chord: the launcher only exists once a frame is composed
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await 50 off_plane || { echo "the launcher chord left the buffer on the plane: $(state)"; exit 1; }
await_imgui '##launcher' || { echo "the launcher never opened over the client"; dump_state; exit 1; }
ctl "key 1 press"; ctl "key 1 release" # Escape
await_no_imgui '##launcher' || { echo "Escape did not close the launcher"; dump_state; exit 1; }
await 100 on_plane || { echo "the buffer did not go back after the launcher closed: $(state)"; exit 1; }

# the brightness OSD
ctl "key 225 press"; ctl "key 225 release" # KEY_BRIGHTNESSUP
brighter() { [[ "$(cat "$XDG_RUNTIME_DIR/backlight/imway0/brightness")" -gt 128 ]]; }
await 50 brighter || { echo "the brightness key did not reach the backlight"; exit 1; }
await 50 off_plane || { echo "the brightness OSD left the buffer on the plane: $(state)"; exit 1; }
await_imgui '##osd' || { echo "the brightness OSD was never drawn"; dump_state; exit 1; }
await 100 on_plane || { echo "the buffer did not go back after the OSD faded: $(state)"; exit 1; }

# the lock screen: the client must not stay on screen over it
ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await 50 off_plane || { echo "the session locked with the client still on the plane: $(state)"; exit 1; }
await_imgui '##lock-overlay' || { echo "the lock overlay was never drawn"; dump_state; exit 1; }
sleep 0.5
off_plane || { echo "the buffer went back to the plane under the lock screen: $(state)"; exit 1; }

expect_alive "compositor died drawing ui over a direct-scanout client"
echo "OK: toast, launcher, OSD and lock screen take the buffer off the plane"
