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
CXXFLAGS="-std=c++23 -O2 -g -DGLFW_INCLUDE_NONE -I$B/protocols -I$B/shaders -Ithird_party/imgui ${CFLAGS} ${CXXFLAGS:-} ${CPPFLAGS:-}"
GLFW_LIB=${GLFW_LIB:--lglfw3}
# -lwayland-client: the screenshot tool puts image/png on the clipboard via a
# wl_data_source off glfw's wl_display (client-side wl_proxy_* + core interfaces)
LIBS="-ldbus-1 -lwayland-server -lwayland-client -lpng $GLFW_LIB -ldrm -linput -ludev -lxkbcommon -lseat -lvulkan -lev -llunasvg -lplutovg -lstd"

# the mixer providers compile to nullptr stubs without their headers
# (__has_include gate); each real path pulls symbols and needs its lib, so
# probe the header and link the lib only then
if echo '#include <sndio.h>' | $CXX $CXXFLAGS -x c++ -E - >/dev/null 2>&1; then
    LIBS="$LIBS -lsndio"
fi

if echo '#include <pulse/pulseaudio.h>' | $CXX $CXXFLAGS -x c++ -E - >/dev/null 2>&1; then
    LIBS="$LIBS -lpulse"
fi

mkdir -p "$B/protocols" "$B/obj" "$B/shaders"

PROTOCOLS="
stable/xdg-shell/xdg-shell.xml
stable/viewporter/viewporter.xml
stable/linux-dmabuf/linux-dmabuf-v1.xml
stable/presentation-time/presentation-time.xml
staging/color-management/color-management-v1.xml
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

# GLSL compute shaders → embedded SPIR-V C arrays (glslangValidator)
SHADERS="
cm_convert.comp
"

for shader in $SHADERS; do
    name=$(basename "$shader" .comp)
    glslangValidator -V "$shader" --variable-name "${name}_spv" -o "$B/shaders/$name.spv.h"
done

IMGUI_SRC="
third_party/imgui/imgui.cpp
third_party/imgui/imgui_draw.cpp
third_party/imgui/imgui_tables.cpp
third_party/imgui/imgui_widgets.cpp
third_party/imgui/imgui_impl_vulkan.cpp
third_party/imgui/imgui_impl_glfw.cpp
"

IMWAY_SRC="
main.cpp
main_screenshot.cpp
composer.cpp
frame_resource.cpp
pooled.cpp
pooled_vk.cpp
scene.cpp
wayland.cpp
control.cpp
input.cpp
keyboard.cpp
dialog.cpp
launcher.cpp
calendar.cpp
inspector.cpp
settings.cpp
shadow.cpp
toast.cpp
history.cpp
dbus_conn.cpp
notifications.cpp
notifier.cpp
mixer.cpp
mixer_sndio.cpp
mixer_pulse.cpp
osd.cpp
wifi.cpp
wifi_iwd.cpp
wifi_nm.cpp
wifi_ui.cpp
icon_store.cpp
icon_pool.cpp
icon.cpp
device_vk.cpp
input_sink.cpp
xdg_utils.cpp
session.cpp
device.cpp
device_kms.cpp
device_headless.cpp
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

# ---- test clients (dev/tests/client_*.c|cpp, run by dev/test.sh) ----

mkdir -p "$B/tests"

# Every protocol a test client might speak: <xml path>|<primary interface
# symbol>. The symbol lets us probe whether the default link already provides
# the wl_interface table (this dev env links libSDL3 statically, which bundles
# xdg-shell and viewporter) — compiling our own copy then is a duplicate
# symbol error, so we only add the generated code for the tables that are
# missing.
CLIENT_PROTOCOLS="
stable/xdg-shell/xdg-shell.xml|xdg_wm_base_interface
stable/viewporter/viewporter.xml|wp_viewporter_interface
stable/linux-dmabuf/linux-dmabuf-v1.xml|zwp_linux_dmabuf_v1_interface
stable/presentation-time/presentation-time.xml|wp_presentation_interface
unstable/xdg-output/xdg-output-unstable-v1.xml|zxdg_output_manager_v1_interface
unstable/xdg-decoration/xdg-decoration-unstable-v1.xml|zxdg_decoration_manager_v1_interface
unstable/primary-selection/primary-selection-unstable-v1.xml|zwp_primary_selection_device_manager_v1_interface
unstable/relative-pointer/relative-pointer-unstable-v1.xml|zwp_relative_pointer_manager_v1_interface
unstable/pointer-constraints/pointer-constraints-unstable-v1.xml|zwp_pointer_constraints_v1_interface
unstable/pointer-gestures/pointer-gestures-unstable-v1.xml|zwp_pointer_gestures_v1_interface
unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml|zwp_keyboard_shortcuts_inhibit_manager_v1_interface
unstable/idle-inhibit/idle-inhibit-unstable-v1.xml|zwp_idle_inhibit_manager_v1_interface
unstable/tablet/tablet-unstable-v2.xml|zwp_tablet_manager_v2_interface
staging/fractional-scale/fractional-scale-v1.xml|wp_fractional_scale_manager_v1_interface
staging/single-pixel-buffer/single-pixel-buffer-v1.xml|wp_single_pixel_buffer_manager_v1_interface
staging/xdg-activation/xdg-activation-v1.xml|xdg_activation_v1_interface
staging/cursor-shape/cursor-shape-v1.xml|wp_cursor_shape_manager_v1_interface
staging/ext-idle-notify/ext-idle-notify-v1.xml|ext_idle_notifier_v1_interface
staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml|wp_linux_drm_syncobj_manager_v1_interface
staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml|xdg_toplevel_icon_manager_v1_interface
staging/color-management/color-management-v1.xml|wp_color_manager_v1_interface
"

GLUE=""

for entry in $CLIENT_PROTOCOLS; do
    xml=${entry%%|*}
    sym=${entry##*|}
    name=$(basename "$xml" .xml)

    wayland-scanner client-header "$PROTO_XML_DIR/$xml" "$B/tests/$name-client-protocol.h"
    wayland-scanner private-code  "$PROTO_XML_DIR/$xml" "$B/tests/$name-client-code.c"

    echo "extern const char $sym[]; int main(void) { return !$sym[0]; }" \
        | $CC -x c - -o "$B/tests/.probe" -lwayland-client ${LDFLAGS:-} 2>/dev/null \
        || GLUE="$GLUE $B/tests/$name-client-code.c"
done

rm -f "$B/tests/.probe"

CLIENT_GLUE_OBJS=""

for src in $GLUE; do
    obj="$B/tests/$(basename "$src" -code.c).o"
    $CC $CFLAGS -I"$B/tests" -c "$src" -o "$obj"
    CLIENT_GLUE_OBJS+="$obj "
done

CMDS=""

for src in dev/tests/client_*.c dev/tests/client_*.cpp; do
    [[ -e "$src" ]] || continue
    name=$(basename "$src")
    name=${name%.*}

    case "$src" in
        *.cpp) CMDS+="$CXX $CXXFLAGS -I$B/tests -o $B/tests/$name $src $CLIENT_GLUE_OBJS -lwayland-client ${LDFLAGS:-}"$'\n' ;;
        *)     CMDS+="$CC $CFLAGS -I$B/tests -o $B/tests/$name $src $CLIENT_GLUE_OBJS -lwayland-client ${LDFLAGS:-}"$'\n' ;;
    esac
done

printf '%s' "$CMDS" | xargs -d '\n' -P "$JOBS" -I{} sh -c '{}'

echo "OK: $B/tests"
