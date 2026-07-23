#!/usr/bin/env bash
# imway-wrap: unshare -rm sh -c 'mount -t tmpfs tmpfs /var/run && exec "$@"' --
# imway-env: AUDIODEVICE=snd/0
# imway-pre: printf 'pcm.!hw {\n@args [ CARD DEV SUBDEV ]\n@args.CARD { type string default "0" }\n@args.DEV { type integer default 0 }\n@args.SUBDEV { type integer default 0 }\ntype null\n}\n' > null.conf; base=$(strings "$(command -v sndiod)" | grep -m1 "^/ix/store/.*share/alsa$"); unshare -U -f sh -c "(ALSA_CONFIG_PATH=$base/alsa.conf:$PWD/null.conf timeout 120 sndiod -dd -f rsnd/0 -U 0 >sndiod.log 2>&1 &)"; for i in $(seq 50); do ls /var/run/sndiod-*/sock0 >/dev/null 2>&1 && break; sleep 0.1; done; d=$(ls -d /var/run/sndiod-* 2>/dev/null | head -1); [ -n "$d" ] && ln -sfn "${d##*/}" /var/run/sndiod && [ -S /var/run/sndiod/sock0 ] || { echo "private sndiod did not come up"; cat sndiod.log 2>/dev/null; exit 1; }
# Volume through a private sndiod: the whole run lives in its own mount
# namespace (tmpfs /var/run, so no host sndiod is visible and parallel runs
# cannot collide), sndiod itself in a nested unmapped userns (its non-root
# path) on the ALSA null device — no hardware, no host audio state. Both
# directions are checked: the volume keys land in sndiod, an external
# change becomes the base of the compositor's next step.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 50 in_log "sndio mixer, level" || {
    echo "mixer did not attach to the private sndiod"
    cat "$IMWAY_LOG" "$XDG_RUNTIME_DIR/sndiod.log" 2>/dev/null
    exit 1
}

level() { sndioctl -n output.level; }

v0=$(level)

# XF86AudioLowerVolume: 5% down through the compositor
ctl "key 114 press"; ctl "key 114 release"
ctl "key 114 press"; ctl "key 114 release"

changed() { [[ "$(level)" != "$v0" ]]; }

if ! await 30 changed; then
    # the level may have started at the bottom rail: go up instead
    ctl "key 115 press"; ctl "key 115 release"
    ctl "key 115 press"; ctl "key 115 release"
    await 30 changed || { echo "volume keys never reached sndiod ($v0)"; exit 1; }
fi

# external change: the compositor must adopt it, not overwrite it with its
# own stale idea of the level on the next step
sndioctl output.level=0.30 >/dev/null
sleep 0.5

ctl "key 114 press"; ctl "key 114 release"

stepped_from_external() {
    python3 - "$(level)" <<'PY'
import sys
v = float(sys.argv[1])
sys.exit(0 if 0.2 <= v <= 0.45 else 1)
PY
}

await 30 stepped_from_external || {
    echo "external level was not adopted (level now $(level))"
    exit 1
}

expect_alive "compositor died with the private sndiod"
echo "OK: volume flows both ways through a private sndiod"
