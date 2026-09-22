#!/usr/bin/env bash
# imway-wrap: unshare -rm sh -c 'mkdir -p /var/run && mount -t tmpfs tmpfs /var/run && { [ ! -d /tmp ] || mount -t tmpfs tmpfs /tmp; } && exec "$@"' --
# imway-env: AUDIODEVICE=snd/0
# imway-pre: printf 'pcm.!hw {\n@args [ CARD DEV SUBDEV ]\n@args.CARD { type string default "0" }\n@args.DEV { type integer default 0 }\n@args.SUBDEV { type integer default 0 }\ntype null\n}\n' > null.conf; base=$(strings "$(command -v sndiod)" | grep -m1 "^/[a-z]*/store/.*share/alsa$" || echo /usr/share/alsa); unshare -U -f sh -c "(ALSA_CONFIG_PATH=$base/alsa.conf:$PWD/null.conf timeout 120 sndiod -dd -f rsnd/0 -U 0 >sndiod.log 2>&1 & echo \$! >$PWD/sndiod.pid)"; for i in $(seq 50); do ls /var/run/sndiod-*/sock0 /tmp/sndio-*/sock0 >/dev/null 2>&1 && break; sleep 0.1; done; d=$(ls -d /var/run/sndiod-* /tmp/sndio-* 2>/dev/null | head -1); [ -n "$d" ] && ln -sfn "${d##*/}" "${d%-*}" && [ -S "${d%-*}/sock0" ] || { echo "private sndiod did not come up"; cat sndiod.log 2>/dev/null; exit 1; }
# The sndio mixer at its limits, on the private sndiod of the feature
# scenario: stepping down past the bottom rail clamps at silence and then
# writes nothing more, the mute key parks the level at zero and restores
# it (sndiod offers no output.mute to switch), and when sndiod goes away
# the compositor says so and the volume keys fall silent.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 50 in_log "sndio mixer, level" || {
    echo "mixer did not attach to the private sndiod"
    cat "$IMWAY_LOG" "$XDG_RUNTIME_DIR/sndiod.log" 2>/dev/null
    exit 1
}

level() { sndioctl -n output.level; }

level_is() { # <lo> <hi>
    python3 - "$(level)" "$1" "$2" <<'PY'
import sys
v, lo, hi = map(float, sys.argv[1:4])
sys.exit(0 if lo <= v <= hi else 1)
PY
}

key() { ctl "key $1 press"; ctl "key $1 release"; }

# twenty-five 5% steps down from wherever sndiod started: the last ones
# clamp at zero and find nothing to write
for i in $(seq 25); do
    key 114
done

await 150 level_is 0 0 || { echo "stepping past the bottom did not clamp at silence ($(level))"; exit 1; }

key 115
await 150 level_is 0.03 0.07 || { echo "one step up from silence did not land ($(level))"; exit 1; }

# soft mute: the level parks at zero, then comes back
key 113
await 150 level_is 0 0 || { echo "the mute key did not park the level ($(level))"; exit 1; }
key 113
await 150 level_is 0.03 0.07 || { echo "unmuting did not restore the level ($(level))"; exit 1; }

# sndiod goes away
kill "$(cat "$XDG_RUNTIME_DIR/sndiod.pid")"
await 150 in_log "sndiod went away, volume control disabled" || { echo "the lost sndiod went unnoticed"; cat "$IMWAY_LOG"; exit 1; }
key 115
key 113
expect_alive "compositor died when sndiod went away"
echo "OK: sndio clamps, soft mute and a vanished sndiod"
