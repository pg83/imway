#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=audio.backend=2 PULSE_SERVER=unix:./pulse/native
# imway-pre: mkdir -p pulse; (pulseaudio -n --daemonize=no --exit-idle-time=-1 --disable-shm=yes --log-target=file:$PWD/pulse/log --load="module-native-protocol-unix socket=$PWD/pulse/native auth-anonymous=1" >/dev/null 2>&1 & echo $! > pulse/pid); for i in $(seq 1 80); do [ -S pulse/native ] && break; sleep 0.1; done; exit 0
# The pulse mixer at its edges, on a private pulseaudio that starts with no
# sink at all: the volume keys have nothing to act on until a null sink
# is loaded, which the compositor picks up from the server's events; the
# volume clamps at both ends; a second sink made the default takes the
# keys from the first; and when pulseaudio dies the compositor reports it
# and the keys fall silent. Skipped where the daemon will not start.
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
    exit 1
}

pa() { pactl --server="$server" "$@"; }

volume() { # <sink>: the first channel's percentage
    pa get-sink-volume "$1" | sed -n 's/.*\/ *\([0-9]*\)% *\/.*/\1/p' | head -1
}

key() { ctl "key $1 press"; ctl "key $1 release"; }

# no sink yet: the keys have nothing to reach
key 115
key 113
sleep 0.3
expect_alive "compositor died pressing volume keys without a sink"

# the first sink appears and becomes the default
pa load-module module-null-sink sink_name=imway_a >/dev/null
pa set-sink-volume imway_a 50%
louder() { [[ "$(volume imway_a)" -gt 50 ]]; }
for _ in 1 2 3 4 5 6 7 8 9 10; do
    key 115
    await 10 louder && break
done
louder || { echo "the keys never reached the sink that appeared ($(volume imway_a))"; exit 1; }

# clamped at the top and at the bottom
pa set-sink-volume imway_a 98%
sleep 0.5 # the compositor learns the level from the server
key 115
top() { [[ "$(volume imway_a)" == 100 ]]; }
await 50 top || { echo "stepping past full did not clamp at 100 ($(volume imway_a))"; exit 1; }
pa set-sink-volume imway_a 2%
sleep 0.5
key 114
bottom() { [[ "$(volume imway_a)" == 0 ]]; }
await 50 bottom || { echo "stepping past silence did not clamp at 0 ($(volume imway_a))"; exit 1; }

# a second sink made the default takes the keys
pa load-module module-null-sink sink_name=imway_b >/dev/null
pa set-sink-volume imway_b 50%
pa set-default-sink imway_b
b_louder() { [[ "$(volume imway_b)" -gt 50 ]]; }
for _ in 1 2 3 4 5 6 7 8 9 10; do
    key 115
    await 10 b_louder && break
done
b_louder || { echo "the keys did not follow the new default sink ($(volume imway_b))"; exit 1; }
[[ "$(volume imway_a)" == 0 ]] || { echo "the old default sink still moved ($(volume imway_a))"; exit 1; }

# pulseaudio dies
kill "$(cat "$XDG_RUNTIME_DIR/pulse/pid")"
await 100 in_log "pulse connection failed" || { echo "the lost server went unreported"; cat "$IMWAY_LOG"; exit 1; }
key 115
key 113
expect_alive "compositor died when pulseaudio went away"
echo "OK: pulse sinks appear, clamp, switch and vanish"
