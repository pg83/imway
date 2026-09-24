#!/usr/bin/env bash
# imway-env: XDG_DATA_HOME=./xdg IMWAY_CHAOS=icon-watch=1
# imway-pre: mkdir -p xdg/icons/hicolor/scalable/apps xdg/applications
# imway-pre: printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#00ff00"/></svg>' > xdg/icons/hicolor/scalable/apps/imway-test-icon.svg
# The icon store gets no inotify instance (the process is out of them): it
# still resolves icons, a change on disk goes unnoticed, and a change of the
# icon theme setting still reloads it.
set -euo pipefail
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_icon_store_reload"
. "$(dirname "$0")/lib.sh"

start_client
wait_client "icon committed"
wait_mapped
gen1=$(dump_field 'title=iconreload' icon_gen)
[[ -n "$gen1" && "$gen1" -gt 0 ]] || { echo "named icon did not resolve without a watch (icon_gen=$gen1)"; exit 1; }

# a watched directory would reload within its half-second settle
touch "$XDG_RUNTIME_DIR/xdg/applications/trigger.desktop"
sleep 1.5
! in_log "icon store reloaded" || { echo "an unwatched icon store reloaded on a change on disk"; cat "$IMWAY_LOG"; exit 1; }

ctl "set appearance.icon_theme Adwaita"
await 100 in_log "icon store reloaded" || { echo "the theme change did not reload the unwatched store"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died with an unwatched icon store"
echo "OK: an icon store without inotify resolves icons and reloads on a theme change"
