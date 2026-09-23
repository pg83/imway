#!/usr/bin/env bash
# A desktop entry repeating its keys is read by the first of each: the first
# Name is what the launcher lists, the first Exec what it runs. A '%' ending
# the Exec line is no field code and stays part of the command, and an
# explicit NoDisplay=false keeps the entry listed.
# imway-env: XDG_DATA_HOME=.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

mkdir -p applications

printf '%s\n' \
    '[Desktop Entry]' \
    'Type=Application' \
    'Name=Quirk Probe Unique' \
    'Name=Renamed Probe Unique' \
    'Exec=./quirk-payload %f 100%' \
    'Exec=./wrong-payload' \
    'Icon=first-icon' \
    'Icon=second-icon' \
    'NoDisplay=false' \
    > applications/quirk-probe.desktop

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf "<%s>" "$@" > "$XDG_RUNTIME_DIR/quirk-args"' \
    > quirk-payload
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'touch "$XDG_RUNTIME_DIR/wrong-ran"' \
    > wrong-payload
chmod +x quirk-payload wrong-payload

ctl "key 125 press"  # Super
ctl "key 60 press"   # F2
ctl "key 60 release"
ctl "key 125 release"
await_typing '##launcher' || { echo "the launcher never took text"; dump_state; exit 1; }
ctl "type Quirk Probe Unique"
await_input "Quirk Probe Unique" || { echo "the field did not take the name"; dump_state; exit 1; }
ctl "key 103 press"; ctl "key 103 release" # Up
ctl "key 28 press"; ctl "key 28 release"   # Enter
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }

await 100 test -s "$XDG_RUNTIME_DIR/quirk-args" || { echo "the entry listed under its first Name did not run its first Exec"; exit 1; }
[[ "$(cat "$XDG_RUNTIME_DIR/quirk-args")" == "<100%>" ]] || { echo "bad arguments: $(cat "$XDG_RUNTIME_DIR/quirk-args")"; exit 1; }
[[ ! -e "$XDG_RUNTIME_DIR/wrong-ran" ]] || { echo "the second Exec ran"; exit 1; }

expect_alive "compositor died reading a quirky desktop entry"
echo "OK: the first of repeated keys wins and a trailing % stays in Exec"
