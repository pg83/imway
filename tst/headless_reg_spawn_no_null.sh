#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=dev-null=1
# imway-args: -- sh -c 'touch boot-ran'
# A host without /dev/null (a chroot, a bare container) leaves the spawner
# nothing to give a child for stdio: the compositor says so at boot and
# starts nothing, neither the `-- CMD` nor what the launcher asks for, and
# carries on without children.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "imway: spawn: /dev/null unavailable: No such file or directory" || { echo "the missing /dev/null went unreported"; cat "$IMWAY_LOG"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "the launcher never took text"; dump_state; exit 1; }
ctl "type touch launched"
await_input "touch launched" || { echo "the launcher never held the command"; dump_state; exit 1; }
ctl "key 28 press"; ctl "key 28 release"
await_no_imgui '##launcher' || { echo "the launcher did not take the command"; exit 1; }

# the boot command went before the control FIFO opened, the launcher's with
# the Enter the dump above already shows handled: had either forked, the
# log would name it by now
! in_log "imway: spawned" || { echo "a child was spawned without /dev/null"; cat "$IMWAY_LOG"; exit 1; }
[[ ! -e "$XDG_RUNTIME_DIR/boot-ran" && ! -e "$XDG_RUNTIME_DIR/launched" ]] || { echo "a spawned command ran"; exit 1; }

expect_alive "compositor died without /dev/null"
echo "OK: without /dev/null the spawner reports it and starts nothing"
