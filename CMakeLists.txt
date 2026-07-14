cmake_minimum_required(VERSION 3.25)
project(imway CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)

pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(DRM            REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(INPUT          REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(UDEV           REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(XKB            REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(SEAT           REQUIRED IMPORTED_TARGET libseat)

# homegrown standard library (github.com/pg83/std), built with make
set(STD_ROOT "" CACHE PATH "root of the std repository (libstd.a must be built)")
if(NOT STD_ROOT)
    message(FATAL_ERROR "-DSTD_ROOT=/path/to/std is required")
endif()
add_library(pgstd STATIC IMPORTED)
set_target_properties(pgstd PROPERTIES IMPORTED_LOCATION "${STD_ROOT}/std/libstd.a")
target_include_directories(pgstd INTERFACE "${STD_ROOT}")

# libev does not install a pkg-config file
find_library(EV_LIBRARY ev REQUIRED)
find_path(EV_INCLUDE_DIR ev.h REQUIRED)
add_library(ev::ev INTERFACE IMPORTED)
target_link_libraries(ev::ev INTERFACE ${EV_LIBRARY})
target_include_directories(ev::ev INTERFACE ${EV_INCLUDE_DIR})

# ---- protocol code generation (xdg-shell) ----
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
set(PROTO_DIR "${CMAKE_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTO_DIR}")

add_custom_command(OUTPUT "${PROTO_DIR}/xdg-shell-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${XDG_SHELL_XML}" "${PROTO_DIR}/xdg-shell-server-protocol.h"
    DEPENDS "${XDG_SHELL_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/xdg-shell-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} client-header "${XDG_SHELL_XML}" "${PROTO_DIR}/xdg-shell-client-protocol.h"
    DEPENDS "${XDG_SHELL_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/xdg-shell-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${XDG_SHELL_XML}" "${PROTO_DIR}/xdg-shell-protocol.c"
    DEPENDS "${XDG_SHELL_XML}")

set(VIEWPORTER_XML "${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/viewporter-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${VIEWPORTER_XML}" "${PROTO_DIR}/viewporter-server-protocol.h"
    DEPENDS "${VIEWPORTER_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/viewporter-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} client-header "${VIEWPORTER_XML}" "${PROTO_DIR}/viewporter-client-protocol.h"
    DEPENDS "${VIEWPORTER_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/viewporter-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${VIEWPORTER_XML}" "${PROTO_DIR}/viewporter-protocol.c"
    DEPENDS "${VIEWPORTER_XML}")

set(DMABUF_XML "${WAYLAND_PROTOCOLS_DIR}/stable/linux-dmabuf/linux-dmabuf-v1.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/linux-dmabuf-v1-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${DMABUF_XML}" "${PROTO_DIR}/linux-dmabuf-v1-server-protocol.h"
    DEPENDS "${DMABUF_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/linux-dmabuf-v1-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} client-header "${DMABUF_XML}" "${PROTO_DIR}/linux-dmabuf-v1-client-protocol.h"
    DEPENDS "${DMABUF_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/linux-dmabuf-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${DMABUF_XML}" "${PROTO_DIR}/linux-dmabuf-v1-protocol.c"
    DEPENDS "${DMABUF_XML}")

set(XDG_DECO_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/xdg-decoration-unstable-v1-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${XDG_DECO_XML}" "${PROTO_DIR}/xdg-decoration-unstable-v1-server-protocol.h"
    DEPENDS "${XDG_DECO_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/xdg-decoration-unstable-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${XDG_DECO_XML}" "${PROTO_DIR}/xdg-decoration-unstable-v1-protocol.c"
    DEPENDS "${XDG_DECO_XML}")

set(PRIMARY_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/primary-selection/primary-selection-unstable-v1.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/primary-selection-unstable-v1-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${PRIMARY_XML}" "${PROTO_DIR}/primary-selection-unstable-v1-server-protocol.h"
    DEPENDS "${PRIMARY_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/primary-selection-unstable-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${PRIMARY_XML}" "${PROTO_DIR}/primary-selection-unstable-v1-protocol.c"
    DEPENDS "${PRIMARY_XML}")

set(CURSOR_SHAPE_XML "${WAYLAND_PROTOCOLS_DIR}/staging/cursor-shape/cursor-shape-v1.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/cursor-shape-v1-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${CURSOR_SHAPE_XML}" "${PROTO_DIR}/cursor-shape-v1-server-protocol.h"
    DEPENDS "${CURSOR_SHAPE_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/cursor-shape-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${CURSOR_SHAPE_XML}" "${PROTO_DIR}/cursor-shape-v1-protocol.c"
    DEPENDS "${CURSOR_SHAPE_XML}")

set(TABLET_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/tablet/tablet-unstable-v2.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/tablet-unstable-v2-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${TABLET_XML}" "${PROTO_DIR}/tablet-unstable-v2-protocol.c"
    DEPENDS "${TABLET_XML}")

set(SPB_XML "${WAYLAND_PROTOCOLS_DIR}/staging/single-pixel-buffer/single-pixel-buffer-v1.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/single-pixel-buffer-v1-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${SPB_XML}" "${PROTO_DIR}/single-pixel-buffer-v1-server-protocol.h"
    DEPENDS "${SPB_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/single-pixel-buffer-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${SPB_XML}" "${PROTO_DIR}/single-pixel-buffer-v1-protocol.c"
    DEPENDS "${SPB_XML}")

set(PRESENTATION_XML "${WAYLAND_PROTOCOLS_DIR}/stable/presentation-time/presentation-time.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/presentation-time-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${PRESENTATION_XML}" "${PROTO_DIR}/presentation-time-server-protocol.h"
    DEPENDS "${PRESENTATION_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/presentation-time-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${PRESENTATION_XML}" "${PROTO_DIR}/presentation-time-protocol.c"
    DEPENDS "${PRESENTATION_XML}")

set(ACTIVATION_XML "${WAYLAND_PROTOCOLS_DIR}/staging/xdg-activation/xdg-activation-v1.xml")
add_custom_command(OUTPUT "${PROTO_DIR}/xdg-activation-v1-server-protocol.h"
    COMMAND ${WAYLAND_SCANNER} server-header "${ACTIVATION_XML}" "${PROTO_DIR}/xdg-activation-v1-server-protocol.h"
    DEPENDS "${ACTIVATION_XML}")
add_custom_command(OUTPUT "${PROTO_DIR}/xdg-activation-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${ACTIVATION_XML}" "${PROTO_DIR}/xdg-activation-v1-protocol.c"
    DEPENDS "${ACTIVATION_XML}")

add_library(protocols STATIC
    "${PROTO_DIR}/xdg-shell-protocol.c"
    "${PROTO_DIR}/xdg-shell-server-protocol.h"
    "${PROTO_DIR}/xdg-shell-client-protocol.h"
    "${PROTO_DIR}/viewporter-protocol.c"
    "${PROTO_DIR}/viewporter-server-protocol.h"
    "${PROTO_DIR}/viewporter-client-protocol.h"
    "${PROTO_DIR}/linux-dmabuf-v1-protocol.c"
    "${PROTO_DIR}/linux-dmabuf-v1-server-protocol.h"
    "${PROTO_DIR}/linux-dmabuf-v1-client-protocol.h"
    "${PROTO_DIR}/xdg-decoration-unstable-v1-protocol.c"
    "${PROTO_DIR}/xdg-decoration-unstable-v1-server-protocol.h"
    "${PROTO_DIR}/primary-selection-unstable-v1-protocol.c"
    "${PROTO_DIR}/primary-selection-unstable-v1-server-protocol.h"
    "${PROTO_DIR}/cursor-shape-v1-protocol.c"
    "${PROTO_DIR}/cursor-shape-v1-server-protocol.h"
    "${PROTO_DIR}/tablet-unstable-v2-protocol.c"
    "${PROTO_DIR}/single-pixel-buffer-v1-protocol.c"
    "${PROTO_DIR}/single-pixel-buffer-v1-server-protocol.h"
    "${PROTO_DIR}/presentation-time-protocol.c"
    "${PROTO_DIR}/presentation-time-server-protocol.h"
    "${PROTO_DIR}/xdg-activation-v1-protocol.c"
    "${PROTO_DIR}/xdg-activation-v1-server-protocol.h")
target_include_directories(protocols PUBLIC "${PROTO_DIR}")

# ---- Dear ImGui (vendored) ----
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC third_party/imgui)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan)

# ---- compositor ----
add_executable(imway
    src/main.cpp
    src/scene.cpp
    src/wayland.cpp
    src/control.cpp
    src/input.cpp
    src/input_sink.cpp
    src/session.cpp
    src/device.cpp
    src/util.cpp
    src/renderer.cpp)
target_link_libraries(imway PRIVATE
    imgui
    protocols
    pgstd
    Vulkan::Vulkan
    PkgConfig::WAYLAND_SERVER
    PkgConfig::DRM
    PkgConfig::INPUT
    PkgConfig::UDEV
    PkgConfig::XKB
    PkgConfig::SEAT
    ev::ev
)

enable_testing()
add_subdirectory(tests)
