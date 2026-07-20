#!/usr/bin/env bash
# A Terminal=true desktop entry is launched through the configured terminal,
# with the desktop Exec command preserved as the terminal child command.
# imway-env: XDG_DATA_HOME=. IMWAY_TERMINAL=./terminal-probe
set -euo pipefail
. "$(dirname "$0")/lib.sh"

mkdir -p applications

printf '%s\n' \
    '[Desktop Entry]' \
    'Type=Application' \
    'Name=Terminal Probe Unique' \
    'Exec=terminal-payload "two words"' \
    'Terminal=true' \
    > applications/terminal-probe.desktop

printf '%s\n' \
    '[Desktop Entry]' \
    'Type=Application' \
    'Name=Direct Probe Unique' \
    'Exec=./direct-payload' \
    'Terminal=false' \
    > applications/direct-probe.desktop

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf "<%s>\n" "$@" > "$XDG_RUNTIME_DIR/terminal-args"' \
    > terminal-probe
chmod +x terminal-probe

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'touch "$XDG_RUNTIME_DIR/direct-ran"' \
    > direct-payload
chmod +x direct-payload

pick_entry() {
    ctl "key 125 press"  # Super
    ctl "key 60 press"   # F2
    ctl "key 60 release"
    ctl "key 125 release"
    sleep 0.2
    ctl "type $1"
    sleep 0.2
    ctl "key 108 press"; ctl "key 108 release" # Down
    ctl "key 28 press"; ctl "key 28 release"   # Enter
}

pick_entry "Terminal Probe Unique"

await 100 test -s "$XDG_RUNTIME_DIR/terminal-args" || {
    echo "Terminal=true entry did not invoke the terminal"
    exit 1
}

expected=$'<-e>\n<sh>\n<-c>\n<terminal-payload "two words">'
actual=$(cat "$XDG_RUNTIME_DIR/terminal-args")

[[ "$actual" == "$expected" ]] || {
    echo "bad terminal arguments"
    printf 'expected:\n%s\nactual:\n%s\n' "$expected" "$actual"
    exit 1
}

pick_entry "Direct Probe Unique"

await 100 test -e "$XDG_RUNTIME_DIR/direct-ran" || {
    echo "Terminal=false entry was not launched directly"
    exit 1
}

echo "OK: launcher honors Terminal=true and Terminal=false"
