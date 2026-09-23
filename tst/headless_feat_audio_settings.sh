#!/usr/bin/env bash
# imway-wrap: unshare -rm sh -c 'mkdir -p /var/run && mount -t tmpfs tmpfs /var/run && { [ ! -d /tmp ] || mount -t tmpfs tmpfs /tmp; } && exec "$@"' --
# imway-env: AUDIODEVICE=snd/0
# imway-pre: printf 'pcm.!hw {\n@args [ CARD DEV SUBDEV ]\n@args.CARD { type string default "0" }\n@args.DEV { type integer default 0 }\n@args.SUBDEV { type integer default 0 }\ntype null\n}\n' > null.conf; base=$(strings "$(command -v sndiod)" | grep -m1 "^/[a-z]*/store/.*share/alsa$" || echo /usr/share/alsa); unshare -U -f sh -c "(ALSA_CONFIG_PATH=$base/alsa.conf:$PWD/null.conf timeout 120 sndiod -dd -f rsnd/0 -U 0 >sndiod.log 2>&1 & echo \$! >$PWD/sndiod.pid)"; for i in $(seq 50); do ls /var/run/sndiod-*/sock0 /tmp/sndio-*/sock0 >/dev/null 2>&1 && break; sleep 0.1; done; d=$(ls -d /var/run/sndiod-* /tmp/sndio-* 2>/dev/null | head -1); [ -n "$d" ] && ln -sfn "${d##*/}" "${d%-*}" && [ -S "${d%-*}/sock0" ] || { echo "private sndiod did not come up"; cat sndiod.log 2>/dev/null; exit 1; }
# The audio settings page with a mixer present, on the private sndiod of
# the mixer scenarios: its muted box and volume slider drive sndiod, the
# volume keys step up to the top rail and clamp there, and with the
# settings dialog up a volume change shows no OSD (the page itself shows the
# level). Coordinates are relative to the settings window, page rows one
# framed widget apart.
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
osd_up() { dump_state | grep -q '^imgui name=##osd '; }
osd_down() { ! osd_up; }

# twenty-five steps up: the last ones clamp at full, and the keys show the OSD
for i in $(seq 25); do
    key 115
done
await 150 level_is 1 1 || { echo "stepping past the top did not clamp at full ($(level))"; exit 1; }
await 50 osd_up || { echo "a volume key showed no OSD"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
at() { # <dx> <dy>
    click_at $((wx + $1)) $((wy + $2))
    ctl "motion $((wx + 600)) $((wy + 480))"
}
at 40 $((38 + 3 * 20)) # the audio page

# muted: the level parks at zero; unticked, it comes back
at 376 43
await 150 level_is 0 0 || { echo "the muted box did not silence sndiod ($(level))"; exit 1; }
at 376 43
await 150 level_is 0.95 1 || { echo "unticking muted did not restore the level ($(level))"; exit 1; }

# the volume slider, clicked a quarter along its 0..100 track
at $((366 + (750 - 366) / 4)) 69
await 150 level_is 0.15 0.35 || { echo "the volume slider did not set sndiod ($(level))"; exit 1; }

# no OSD for a key while the dialog shows the level; the one from before
# has to time out first
await 100 osd_down || { echo "the OSD never went away"; exit 1; }
key 114
await 150 level_is 0.1 0.3 || { echo "the volume key did not step with settings open ($(level))"; exit 1; }
sleep 0.3
osd_down || { echo "a volume key showed the OSD over the settings dialog"; exit 1; }

expect_alive "compositor died on the audio settings page"
echo "OK: the audio page's mute and volume drive sndiod, keys clamp at full"
