#!/usr/bin/env bash
# A named xdg-toplevel-icon must be re-resolved when the icon store reloads:
# the old resolution points into the store generation the reload deletes.
# imway-env: XDG_DATA_HOME=./xdg
# imway-pre: mkdir -p xdg/icons/hicolor/scalable/apps xdg/applications
# imway-pre: printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#00ff00"/></svg>' > xdg/icons/hicolor/scalable/apps/imway-test-icon.svg
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "icon committed"
wait_mapped
sleep 0.3

gen1=$(dump_field 'title=iconreload' icon_gen)
[[ -n "$gen1" && "$gen1" -gt 0 ]] || {
    echo "named icon did not resolve (icon_gen=$gen1)"; exit 1; }

# any churn in a watched xdg dir triggers the store reload (0.5s settle)
touch "$XDG_RUNTIME_DIR/xdg/applications/trigger.desktop"
await 100 in_log "icon store reloaded" || {
    echo "icon store did not reload"; exit 1; }
sleep 0.2

gen2=$(dump_field 'title=iconreload' icon_gen)

echo "icon_gen before=$gen1 after=$gen2"

# the old generation is gone: the window must hold a freshly resolved icon,
# not the stale (freed) one and not free-list garbage
[[ -n "$gen2" && "$gen2" -gt "$gen1" && "$gen2" -lt $((gen1 + 1000)) ]] || {
    echo "toplevel still holds the pre-reload icon (dangling generation)"; exit 1; }

echo "OK: named toplevel icon re-resolved across the store reload"
