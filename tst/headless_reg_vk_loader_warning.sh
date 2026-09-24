#!/usr/bin/env bash
# The Vulkan loader's warnings reach the compositor's log, those it gives
# while it builds the instance included: a driver manifest whose library is
# not there is skipped with a warning, and the session boots on the drivers
# that are.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

manifest="$XDG_RUNTIME_DIR/absent_icd.json"
cat >"$manifest" <<'JSON'
{"file_format_version": "1.0.0", "ICD": {"library_path": "libvulkan_imway_absent.so", "api_version": "1.3.0"}}
JSON

kms_boot VK_ADD_DRIVER_FILES="$manifest" --
boot_rc 0 "a driver manifest without its library"
boot_has "^vk: .*libvulkan_imway_absent" "a driver manifest without its library"
boot_has "clean exit after" "a driver manifest without its library"

expect_alive "the scenario's own compositor died"
echo "OK: the loader's warnings go to the log"
