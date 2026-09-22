#!/usr/bin/env bash
# private-session-bus
# imway-pre: "$IMWAY_CLIENT" >"$XDG_RUNTIME_DIR/names.log" 2>&1 & for i in $(seq 50); do grep -q "names held" "$XDG_RUNTIME_DIR/names.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "the name holder did not come up"; exit 1
# Another desktop already owns the appmenu registrar, the tray watcher and
# the notification service: the compositor says so, leaves them to their
# owner, and its dock shows no tray while the bar keeps running.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "AppMenu registrar is taken" || { echo "the taken registrar went unnoticed"; cat "$IMWAY_LOG"; exit 1; }
in_log "StatusNotifierWatcher is taken" || { echo "the taken watcher went unnoticed"; cat "$IMWAY_LOG"; exit 1; }
in_log "org.freedesktop.Notifications is taken" || { echo "the taken notification service went unnoticed"; cat "$IMWAY_LOG"; exit 1; }

# the dock keeps drawing without a tray of its own
first=$(dump_field '^frames done' done)
frames_advanced() { [[ "$(dump_field '^frames done' done)" -gt "$first" ]]; }
ctl "motion 20 400"
await 50 frames_advanced || { echo "the compositor stopped drawing"; exit 1; }
[[ -z "$(dump_state | grep '^tray ')" ]] || { echo "a tray appeared without the watcher"; dump_state; exit 1; }

expect_alive "compositor died next to another desktop's services"
echo "OK: taken session names are reported and left alone"
