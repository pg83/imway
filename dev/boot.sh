#!/usr/bin/env bash
# Bootstrap build: no cmake, no pkg-config, no detection.
# Assumes every header and library (wayland, drm, input, udev, xkbcommon,
# seat, vulkan, ev, std) is on the compiler's default paths, and that
# wayland-scanner is on PATH. Protocol XMLs are taken from
# /usr/share/wayland-protocols (override with WL_PROTOCOL_DIR).

set -euo pipefail
cd "$(dirname "$0")/.."

CC=${CC:-cc}
CXX=${CXX:-c++}
B=${B:-build-boot}
PROTO_XML_DIR=${WL_PROTOCOL_DIR:-/usr/share/wayland-protocols}
JOBS=$(nproc 2>/dev/null || echo 4)

CFLAGS="-O2 -g -I$B/protocols ${CFLAGS:-} ${CPPFLAGS:-}"
CXXFLAGS="-std=c++23 -O2 -g -I$B/protocols -Ithird_party/imgui ${CFLAGS} ${CXXFLAGS:-} ${CPPFLAGS:-}"
LIBS="-lwayland-server -ldrm -linput -ludev -lxkbcommon -lseat -lvulkan -lev -llunasvg -lplutovg -lstd"

mkdir -p "$B/protocols" "$B/obj"

PROTOCOLS="
stable/xdg-shell/xdg-shell.xml
stable/viewporter/viewporter.xml
stable/linux-dmabuf/linux-dmabuf-v1.xml
stable/presentation-time/presentation-time.xml
unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
unstable/primary-selection/primary-selection-unstable-v1.xml
unstable/tablet/tablet-unstable-v2.xml
staging/cursor-shape/cursor-shape-v1.xml
staging/single-pixel-buffer/single-pixel-buffer-v1.xml
staging/xdg-activation/xdg-activation-v1.xml
unstable/xdg-output/xdg-output-unstable-v1.xml
staging/fractional-scale/fractional-scale-v1.xml
unstable/pointer-gestures/pointer-gestures-unstable-v1.xml
unstable/relative-pointer/relative-pointer-unstable-v1.xml
unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml
unstable/idle-inhibit/idle-inhibit-unstable-v1.xml
staging/ext-idle-notify/ext-idle-notify-v1.xml
staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml
staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml
"

for xml in $PROTOCOLS; do
    name=$(basename "$xml" .xml)
    wayland-scanner server-header "$PROTO_XML_DIR/$xml" "$B/protocols/$name-server-protocol.h"
    wayland-scanner private-code  "$PROTO_XML_DIR/$xml" "$B/protocols/$name-protocol.c"
done

IMGUI_SRC="
third_party/imgui/imgui.cpp
third_party/imgui/imgui_draw.cpp
third_party/imgui/imgui_tables.cpp
third_party/imgui/imgui_widgets.cpp
third_party/imgui/imgui_impl_vulkan.cpp
"

IMWAY_SRC="
main.cpp
scene.cpp
wayland.cpp
control.cpp
input.cpp
keyboard.cpp
launcher.cpp
calendar.cpp
inspector.cpp
settings.cpp
icon_store.cpp
icon_pool.cpp
icon.cpp
device_vk.cpp
input_sink.cpp
xdg_utils.cpp
session.cpp
device.cpp
util.cpp
output.cpp
frame_listener.cpp
renderer.cpp
"

OBJS=""
CMDS=""

for src in "$B"/protocols/*.c; do
    obj="$B/obj/$(basename "$src" .c).o"
    CMDS+="$CC $CFLAGS -c $src -o $obj"$'\n'
    OBJS+="$obj "
done

for src in $IMGUI_SRC $IMWAY_SRC; do
    obj="$B/obj/$(basename "$src" .cpp).o"
    CMDS+="$CXX $CXXFLAGS -c $src -o $obj"$'\n'
    OBJS+="$obj "
done

printf '%s' "$CMDS" | xargs -d '\n' -P "$JOBS" -I{} sh -c '{}'

$CXX -o "$B/imway" $OBJS $LIBS ${LDFLAGS:-} ${CTRFLAGS:-}
echo "OK: $B/imway"
