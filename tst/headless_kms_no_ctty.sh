#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The KMS session opens its VT to switch the keyboard off and the console
# to graphics, but must not take that VT as its controlling terminal: a
# compositor started without one (a service, an ssh session, this runner's
# own session) would otherwise die of the SIGHUP the next hangup of the
# console delivers — the "exited during startup" a parallel run hit.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

# field 7 of /proc/<pid>/stat is the controlling tty's device number; the
# command name before it is parenthesised and may hold spaces
tty_nr=$(sed 's/.*) //' "/proc/$IMWAY_PID/stat" | awk '{ print $5 }')
[[ "$tty_nr" == 0 ]] || { echo "the compositor took tty device $tty_nr as its controlling terminal"; exit 1; }

expect_alive "compositor died opening its VT"
echo "OK: the VT is used, never adopted as the controlling terminal"
