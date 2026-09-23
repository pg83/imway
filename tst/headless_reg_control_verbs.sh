#!/usr/bin/env bash
# The control FIFO's own edges, each answered in the log and survived: an
# unknown verb, an unknown setting, a line longer than the reader's buffer
# (cut at 1023 bytes, the rest dropped until the newline), a dump whose
# final path is a directory (the temporary file cannot be renamed onto it),
# and "type" text with characters no key produces, which are skipped: the
# launcher still finds settings from the letters around them.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "no-such-verb 1 2"
await 50 in_log "imway: unknown command: no-such-verb 1 2" || { echo "an unknown verb was not reported"; cat "$IMWAY_LOG"; exit 1; }

ctl "set no.such.setting 1"
await 50 in_log "imway: control: unknown setting no.such.setting" || { echo "an unknown setting was not reported"; exit 1; }

long=$(printf 'x%.0s' $(seq 1100))
ctl "$long"
cut_line() {
    local n
    n=$(grep -o "imway: unknown command: x*" "$IMWAY_LOG" | awk '{ print length($4) }' | tail -1)
    [[ "$n" == 1023 ]]
}
await 50 cut_line || { echo "an overlong line was not cut at 1023 bytes"; exit 1; }

mkdir -p "$XDG_RUNTIME_DIR/dumpdir/inside"
ctl "dump $XDG_RUNTIME_DIR/dumpdir"
await 50 in_log "imway: dump: cannot rename" || { echo "a dump onto a directory was not reported"; cat "$IMWAY_LOG"; exit 1; }
[[ ! -e "$XDG_RUNTIME_DIR/dumpdir.tmp" ]] || { echo "the failed dump left its temporary file"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "the launcher did not open"; exit 1; }
ctl "type sett§ings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "the text with an untypeable character did not reach the launcher"; dump_state; exit 1; }

expect_alive "compositor died on the control FIFO's edges"
echo "OK: unknown verbs and settings, an overlong line, a failed dump rename and untypeable text"
