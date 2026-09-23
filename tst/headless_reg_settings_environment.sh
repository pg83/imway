#!/usr/bin/env bash
# imway-env: IMWAY_SETTINGS=;no-equals-here;;desktop.dock_position=2 XDG_DATA_HOME=./xdg XDG_DATA_DIRS=./vk TERMINAL=./terminal-probe
# imway-pre: mkdir -p vk xdg/applications && ln -s /usr/share/vulkan vk/vulkan
# imway-pre: printf '[Desktop Entry]\nType=Application\nName=Env Terminal Probe\nExec=env-payload\nTerminal=true\n' > xdg/applications/env-terminal.desktop
# imway-pre: printf '#!/bin/sh\nprintf "<%%s>" "$@" > "$XDG_RUNTIME_DIR/terminal-args"\n' > terminal-probe && chmod +x terminal-probe
# Settings from the environment at startup: IMWAY_SETTINGS skips its empty
# entries and the one without an "=", and still applies the rest (the
# dock comes up on the top edge); with IMWAY_TERMINAL unset the plain
# TERMINAL variable names the terminal a Terminal=true entry runs in.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

dock_on_top() { [[ "$(dump_field '^imgui name=##dock ' y)" == 0 && "$(dump_field '^imgui name=##dock ' w)" -gt 600 ]]; }
await 50 dock_on_top || { echo "the IMWAY_SETTINGS dock position was not applied: $(dump_state | grep '##dock')"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type Env Terminal"
await_input "Env Terminal" || { echo "the query did not land"; exit 1; }
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await 100 test -s terminal-args || { echo "the Terminal=true entry did not run in \$TERMINAL"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(cat terminal-args)" == "<-e><sh><-c><env-payload>" ]] || { echo "bad terminal arguments: $(cat terminal-args)"; exit 1; }

expect_alive "compositor died applying settings from the environment"
echo "OK: IMWAY_SETTINGS skips malformed entries, TERMINAL names the terminal"
