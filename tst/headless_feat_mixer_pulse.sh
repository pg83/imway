#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=audio.backend=2 PULSE_SERVER=unix:./pulse/native
# imway-pre: mkdir -p pulse; (pulseaudio -n --daemonize=no --exit-idle-time=-1 --disable-shm=yes --log-target=file:$PWD/pulse/log --load="module-native-protocol-unix socket=$PWD/pulse/native auth-anonymous=1" --load="module-null-sink sink_name=imway_null" >/dev/null 2>&1 &); for i in $(seq 1 80); do [ -S pulse/native ] && break; sleep 0.1; done; exit 0
# Volume through a private pulseaudio on a null sink: the compositor's
# volume keys land in the server, an external change becomes the base of the
# next step, and mute round trips. Skipped where the daemon will not start.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

server="unix:$XDG_RUNTIME_DIR/pulse/native"

[[ -S "$XDG_RUNTIME_DIR/pulse/native" ]] || {
    echo "no private pulseaudio, skipping"
    cat "$XDG_RUNTIME_DIR/pulse/log" 2>/dev/null || true
    exit 127
}
command -v pactl >/dev/null || { echo "no pactl, skipping"; exit 127; }

await 50 in_log "pulse mixer" || {
    echo "the mixer did not attach to the private pulseaudio"
    cat "$IMWAY_LOG"
    cat "$XDG_RUNTIME_DIR/pulse/log" 2>/dev/null || true
    exit 1
}

pa() { pactl --server="$server" "$@"; }

# the percentage of the first channel, as an integer
volume() {
    pa get-sink-volume imway_null | sed -n 's/.*\/ *\([0-9]*\)% *\/.*/\1/p' | head -1
}
muted() {
    pa get-sink-mute imway_null | awk '{print $2}'
}

pa set-sink-volume imway_null 50% || { echo "pactl cannot set the volume"; exit 1; }
pa set-sink-mute imway_null 0

start=$(volume)
[[ "$start" -ge 45 && "$start" -le 55 ]] || { echo "the sink did not take 50% ($start)"; exit 1; }

osd_up() {
    [[ -n "$(dump_field '^imgui name=##osd' x)" ]]
}

# a volume key steps the sink up by the configured 5% and shows an OSD
ctl "key 115 press"; ctl "key 115 release" # KEY_VOLUMEUP
louder() { [[ "$(volume)" -gt "$start" ]]; }
await 50 louder || { echo "the volume key did not reach the server ($start -> $(volume))"; exit 1; }
await 50 osd_up || { echo "no on-screen display for the volume"; dump_state; exit 1; }

up=$(volume)

ctl "key 114 press"; ctl "key 114 release" # KEY_VOLUMEDOWN
quieter() { [[ "$(volume)" -lt "$up" ]]; }
await 50 quieter || { echo "the volume did not come back down ($up -> $(volume))"; exit 1; }

# an external change is the base of the next step, so the compositor tracks
# the server rather than its own idea of the level
pa set-sink-volume imway_null 20%
await 50 test "$(volume)" -le 25 || true
ctl "key 115 press"; ctl "key 115 release"
stepped_from_external() {
    local v
    v=$(volume)
    [[ "$v" -gt 20 && "$v" -lt 35 ]]
}
await 50 stepped_from_external || { echo "the step ignored the external level ($(volume))"; exit 1; }

# mute round trips
ctl "key 113 press"; ctl "key 113 release" # KEY_MUTE
is_muted() { [[ "$(muted)" == yes ]]; }
await 50 is_muted || { echo "the mute key did not reach the server ($(muted))"; exit 1; }
ctl "key 113 press"; ctl "key 113 release"
is_unmuted() { [[ "$(muted)" == no ]]; }
await 50 is_unmuted || { echo "the mute key did not toggle back ($(muted))"; exit 1; }

expect_alive "compositor died driving pulseaudio"
echo "OK: volume and mute round trip through a private pulseaudio"
