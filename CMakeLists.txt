cmake_minimum_required(VERSION 3.25)
project(imway CXX C)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "imway's libstd dependency currently requires Clang; configure from `nix develop`")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IMWAY_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(PGSTD_EXTRA "-Wno-error=unused-result")
if(IMWAY_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "IMWAY_SANITIZERS requires Clang or GCC")
    endif()

    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    string(APPEND PGSTD_EXTRA " -fsanitize=address,undefined -fno-omit-frame-pointer")
endif()

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)

pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(DRM            REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(INPUT          REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(UDEV           REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(XKB            REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(SEAT           REQUIRED IMPORTED_TARGET libseat)
pkg_check_modules(DBUS           REQUIRED IMPORTED_TARGET dbus-1)
pkg_check_modules(GLFW           REQUIRED IMPORTED_TARGET glfw3)
pkg_check_modules(PNG            REQUIRED IMPORTED_TARGET libpng)

# optional mixer backends: each source self-gates on __has_include, so we
# only expose the header (for the gate) and the lib (for linking) when it is
# actually installed. sndio ships no pkg-config, pulse does
find_library(SNDIO_LIBRARY sndio)
find_path(SNDIO_INCLUDE_DIR sndio.h)
if(SNDIO_LIBRARY AND SNDIO_INCLUDE_DIR)
    add_library(sndio::sndio INTERFACE IMPORTED)
    target_link_libraries(sndio::sndio INTERFACE ${SNDIO_LIBRARY})
    target_include_directories(sndio::sndio INTERFACE ${SNDIO_INCLUDE_DIR})
    set(HAVE_SNDIO ON)
    message(STATUS "imway: sndio mixer enabled")
endif()

pkg_check_modules(PULSE IMPORTED_TARGET libpulse)
if(PULSE_FOUND)
    message(STATUS "imway: pulse mixer enabled (pulseaudio/pipewire)")
endif()

# homegrown standard library (github.com/pg83/std)
option(IMWAY_USE_VENDORED_STD "Build libstd from third_party/libstd; OFF = link the system-installed one with -lstd" ON)

if(IMWAY_USE_VENDORED_STD)
    set(STD_ROOT "${CMAKE_SOURCE_DIR}/third_party/libstd")
    set(STD_BUILD_ROOT "${CMAKE_BINARY_DIR}/libstd")
    file(GLOB_RECURSE STD_INPUTS CONFIGURE_DEPENDS
        "${STD_ROOT}/std/*.cpp"
        "${STD_ROOT}/std/*.h")
    file(MAKE_DIRECTORY "${STD_BUILD_ROOT}")
    file(GENERATE OUTPUT "${STD_BUILD_ROOT}/build-flags.txt"
        CONTENT "CXX=${CMAKE_CXX_COMPILER}\nEXTRA=${PGSTD_EXTRA}\n")

    include(ProcessorCount)
    ProcessorCount(NCPU)
    if(NCPU EQUAL 0)
        set(NCPU 2)
    endif()

    add_custom_command(OUTPUT "${STD_BUILD_ROOT}/std/libstd.a"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${STD_ROOT}/std" "${STD_BUILD_ROOT}/std"
        COMMAND ${CMAKE_COMMAND} -E copy "${STD_ROOT}/Makefile" "${STD_BUILD_ROOT}/Makefile"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STD_BUILD_ROOT}/dev"
        COMMAND ${CMAKE_COMMAND} -E touch "${STD_BUILD_ROOT}/dev/build.sh" "${STD_BUILD_ROOT}/dev/run.sh"
        COMMAND make -C "${STD_BUILD_ROOT}" clear
        COMMAND make -C "${STD_BUILD_ROOT}" -j${NCPU} CXX=${CMAKE_CXX_COMPILER} "EXTRA=${PGSTD_EXTRA}" std/libstd.a
        DEPENDS ${STD_INPUTS} "${STD_ROOT}/Makefile" "${STD_BUILD_ROOT}/build-flags.txt"
        COMMENT "building vendored libstd")
    add_custom_target(pgstd_build DEPENDS "${STD_BUILD_ROOT}/std/libstd.a")

    add_library(pgstd STATIC IMPORTED)
    set_target_properties(pgstd PROPERTIES IMPORTED_LOCATION "${STD_BUILD_ROOT}/std/libstd.a")
    target_include_directories(pgstd INTERFACE "${STD_ROOT}")
    add_dependencies(pgstd pgstd_build)
else()
    # system libstd: headers on the compiler's default include path, plain -lstd
    add_library(pgstd INTERFACE)
    target_link_libraries(pgstd INTERFACE std)
endif()

find_library(LUNASVG_LIBRARY lunasvg REQUIRED)
find_library(PLUTOVG_LIBRARY plutovg REQUIRED)
find_path(LUNASVG_INCLUDE_DIR lunasvg.h PATH_SUFFIXES lunasvg REQUIRED)
add_library(lunasvg::lunasvg INTERFACE IMPORTED)
target_link_libraries(lunasvg::lunasvg INTERFACE ${LUNASVG_LIBRARY} ${PLUTOVG_LIBRARY})
target_include_directories(lunasvg::lunasvg INTERFACE ${LUNASVG_INCLUDE_DIR})

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

foreach(PROTO
        "staging/color-management/color-management-v1"
        "staging/xdg-toplevel-icon/xdg-toplevel-icon-v1"
        "unstable/xdg-output/xdg-output-unstable-v1"
        "staging/fractional-scale/fractional-scale-v1"
        "unstable/pointer-gestures/pointer-gestures-unstable-v1"
        "unstable/relative-pointer/relative-pointer-unstable-v1"
        "unstable/pointer-constraints/pointer-constraints-unstable-v1"
        "unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1"
        "unstable/idle-inhibit/idle-inhibit-unstable-v1"
        "staging/ext-idle-notify/ext-idle-notify-v1"
        "staging/linux-drm-syncobj/linux-drm-syncobj-v1")
    get_filename_component(PROTO_NAME "${PROTO}" NAME)
    set(PROTO_XML "${WAYLAND_PROTOCOLS_DIR}/${PROTO}.xml")
    add_custom_command(OUTPUT "${PROTO_DIR}/${PROTO_NAME}-server-protocol.h"
        COMMAND ${WAYLAND_SCANNER} server-header "${PROTO_XML}" "${PROTO_DIR}/${PROTO_NAME}-server-protocol.h"
        DEPENDS "${PROTO_XML}")
    add_custom_command(OUTPUT "${PROTO_DIR}/${PROTO_NAME}-protocol.c"
        COMMAND ${WAYLAND_SCANNER} private-code "${PROTO_XML}" "${PROTO_DIR}/${PROTO_NAME}-protocol.c"
        DEPENDS "${PROTO_XML}")
    list(APPEND EXTRA_PROTO_SRC
        "${PROTO_DIR}/${PROTO_NAME}-server-protocol.h"
        "${PROTO_DIR}/${PROTO_NAME}-protocol.c")
endforeach()

add_library(protocols STATIC
    ${EXTRA_PROTO_SRC}
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
    third_party/imgui/imgui_impl_vulkan.cpp
    third_party/imgui/imgui_impl_glfw.cpp)
target_include_directories(imgui PUBLIC third_party/imgui)
# GLFW_INCLUDE_NONE keeps glfw3.h from pulling GL/X11 headers (we build glfw
# wayland+vulkan-only); it propagates to imway so main_screenshot.cpp sees it
target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan PkgConfig::GLFW)

# ---- compositor ----
add_executable(imway
    main.cpp
    main_composer.cpp
    main_screenshot.cpp
    composer.cpp
    scene.cpp
    frame_resource.cpp
    pooled.cpp
    pooled_ev.cpp
    pooled_fd.cpp
    pooled_vk.cpp
    wayland.cpp
    control.cpp
    input.cpp
    input_sink.cpp
    intr_list.cpp
    listener.cpp
    keyboard.cpp
    dialog.cpp
    launcher.cpp
    calendar.cpp
    inspector.cpp
    settings.cpp
    shadow.cpp
    toast.cpp
    history.cpp
    osd.cpp
    dbus_conn.cpp
    notifications.cpp
    notifier.cpp
    mixer.cpp
    mixer_sndio.cpp
    mixer_pulse.cpp
    wifi.cpp
    wifi_iwd.cpp
    wifi_nm.cpp
    wifi_ui.cpp
    icon.cpp
    icon_pool.cpp
    icon_store.cpp
    xdg_utils.cpp
    session.cpp
    device.cpp
    device_vk.cpp
    device_kms.cpp
    device_headless.cpp
    util.cpp
    output.cpp
    frame_listener.cpp
    renderer.cpp)
target_link_libraries(imway PRIVATE
    imgui
    protocols
    pgstd
    Vulkan::Vulkan
    lunasvg::lunasvg
    PkgConfig::WAYLAND_SERVER
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::DRM
    PkgConfig::INPUT
    PkgConfig::UDEV
    PkgConfig::XKB
    PkgConfig::SEAT
    PkgConfig::DBUS
    PkgConfig::GLFW
    PkgConfig::PNG
    ev::ev
)

if(HAVE_SNDIO)
    target_link_libraries(imway PRIVATE sndio::sndio)
endif()

if(PULSE_FOUND)
    target_link_libraries(imway PRIVATE PkgConfig::PULSE)
endif()
