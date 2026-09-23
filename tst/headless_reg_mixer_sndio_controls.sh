#!/usr/bin/env bash
# imway-wrap: unshare -rm sh -c 'mkdir -p /var/run && mount -t tmpfs tmpfs /var/run && { [ ! -d /tmp ] || mount -t tmpfs tmpfs /tmp; } && exec "$@"' --
# imway-env: AUDIODEVICE=snd/0
# imway-pre: ("$IMWAY_CLIENT" serve >sndio-fake.log 2>&1 &); for i in $(seq 50); do grep -qs ready sndio-events && break; sleep 0.1; done; grep -qs ready sndio-events || { cat sndio-fake.log; exit 1; }
# The sndio mixer on a server with a hardware-like control set (a stand-in
# sndiod, see the client): of all the controls only the server's own
# output.level and output.mute are taken, the mute key switches the mute
# control instead of parking the level, a mute change from outside shows
# up, and when the server takes the controls away the mixer falls back to
# the soft mute and then leaves the level alone; a control offered later
# is taken up.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

events() { cat "$XDG_RUNTIME_DIR/sndio-events" 2>/dev/null || true; }
has_event() { events | grep -qx "$1"; }
fake() { echo "$1" > "$XDG_RUNTIME_DIR/sndio-ctl"; }
key() { ctl "key $1 press"; ctl "key $1 release"; }

mixer_is() { # <volume> <muted>
    [[ "$(dump_field '^mixer volume' volume)" == "$1" && "$(dump_field '^mixer volume' muted)" == "$2" ]]
}

fail() {
    echo "$1"
    dump_state | grep '^mixer' || true
    events
    cat "$IMWAY_LOG"
    exit 1
}

pongs_above() { [[ "$(events | grep -c '^pong$' || true)" -gt "$1" ]]; }

# every write the compositor made before this is in the event log after it
sync_fake() {
    local n
    n=$(events | grep -c '^pong$' || true)
    fake ping
    await 100 pongs_above "$n" || fail "the stand-in sndiod stopped answering"
}

await 50 in_log "sndio mixer, level 5, mute 6" || fail "the mixer did not pick the server's level and mute controls"
await 50 mixer_is 79 0 || fail "the mixer does not show the server's level"

# the mute key switches output.mute; the level stays where it is
key 113
await 100 has_event "set 6 1" || fail "the mute key did not switch the mute control"
await 100 mixer_is 79 1 || fail "the mixer did not show itself muted"
key 113
await 100 has_event "set 6 0" || fail "the mute key did not switch the mute control back"
await 100 mixer_is 79 0 || fail "the mixer did not show itself unmuted"
sync_fake
! events | grep -q "^set 5 " || fail "the mute key touched the level"

# a mute from outside (another mixer program) is news; so is a value on
# a control the mixer does not use, which it ignores
fake "val 8 3"
fake "val 6 1"
await 100 mixer_is 79 1 || fail "a mute from outside did not show"
fake "val 6 0"
await 100 mixer_is 79 0 || fail "an unmute from outside did not show"

# the server takes output.mute away: the mute key parks the level instead.
# A level change from outside after it shows the compositor has read both
fake "del 6"
fake "val 5 90"
await 100 mixer_is 71 0 || fail "the level change after the mute control went did not show"
key 113
await 100 has_event "set 5 0" || fail "without a mute control the mute key did not park the level"
await 100 mixer_is 0 1 || fail "the soft mute did not show"
key 113
await 100 has_event "set 5 90" || fail "the soft mute did not restore the level"
await 100 mixer_is 71 0 || fail "the restored level did not show"

# then output.level too, and the mute control comes back already on: the
# volume keys have nothing left to write
fake "del 5"
fake "val 6 1"
fake "add 6"
await 100 mixer_is 71 1 || fail "the returning mute control did not show"
sync_fake
before=$(events | grep -c '^set ' || true)
key 115
key 114
dump_state >/dev/null
sync_fake
[[ "$(events | grep -c '^set ' || true)" == "$before" ]] || fail "the volume keys wrote to a control the server took away"

expect_alive "compositor died as the sndio controls changed"
echo "OK: the sndio mixer uses the server's mute and level controls and follows them away"
