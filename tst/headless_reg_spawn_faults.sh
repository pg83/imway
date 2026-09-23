#!/usr/bin/env bash
# imway-pre: printf 'no interpreter line, no ELF header\n' > not-a-program && chmod +x not-a-program
# imway-args: -- ./not-a-program
# A child whose program the kernel will not execute (executable, but
# neither ELF nor script) ends with 127 like a missing one, and a fork the
# process limit refuses is reported instead of taking the compositor down;
# once the limit is back, the launcher spawns again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

launch() { # <command typed into the launcher>
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release"
    await_typing '##launcher' || { echo "the launcher never took text"; dump_state; exit 1; }
    ctl "type $1"
    await_input "$1" || { echo "the launcher never held \"$1\""; dump_state; exit 1; }
    ctl "key 28 press"; ctl "key 28 release"
    await_no_imgui '##launcher' || { echo "the launcher did not run \"$1\""; exit 1; }
}

nproc_soft() { # <limit|restore>: the compositor's RLIMIT_NPROC soft limit
    python3 - "$IMWAY_PID" "$1" "$XDG_RUNTIME_DIR/nproc" <<'PY'
import resource, sys
pid, what, saved = int(sys.argv[1]), sys.argv[2], sys.argv[3]
soft, hard = resource.prlimit(pid, resource.RLIMIT_NPROC)
if what == "restore":
    resource.prlimit(pid, resource.RLIMIT_NPROC, (int(open(saved).read()), hard))
else:
    open(saved, "w").write(str(soft))
    resource.prlimit(pid, resource.RLIMIT_NPROC, (int(what), hard))
PY
}

pid=$(grep -o "imway: spawned [0-9]*: ./not-a-program" "$IMWAY_LOG" | awk '{print $3}' | tr -d :) || true
[[ -n "$pid" ]] || { echo "the boot command was not spawned"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "imway: child $pid exited with status 127" || { echo "the unexecutable child did not end with 127"; cat "$IMWAY_LOG"; exit 1; }

# root ignores the process limit
[[ "$(id -u)" != 0 ]] || { echo "SKIP: running as root, RLIMIT_NPROC does not bind"; exit 127; }

# one process per user: this user already runs more, so every fork fails
nproc_soft 1
launch "touch refused"
await 100 in_log "imway: spawn: fork failed: Resource temporarily unavailable" || { echo "the refused fork went unreported"; cat "$IMWAY_LOG"; exit 1; }
nproc_soft restore
[[ ! -e "$XDG_RUNTIME_DIR/refused" ]] || { echo "the refused command ran"; exit 1; }

launch "touch allowed"
await 100 test -e "$XDG_RUNTIME_DIR/allowed" || { echo "the launcher did not spawn once the limit was back"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on a refused fork"
echo "OK: an unexecutable child ends with 127 and a refused fork is reported"
