import build
import glob
import os
import shlex


def words(value):
    return shlex.split(value) if value else []


common_cflags = ["-O2", "-g", *words(os.environ.get("CFLAGS")), *words(os.environ.get("CPPFLAGS"))]
common_cxxflags = [
    "-std=c++23", "-O2", "-g", "-DGLFW_INCLUDE_NONE",
    *words(os.environ.get("CFLAGS")), *words(os.environ.get("CXXFLAGS")),
    *words(os.environ.get("CPPFLAGS")),
]
common_ldflags = [*words(os.environ.get("LDFLAGS")), *words(os.environ.get("CTRFLAGS"))]

build.includes += [
    "$(S)/third_party/imgui",
    "$(B)/protocols",
    "$(B)/shaders",
    "$(B)/tests",
]


wayland_server = pkg_config("wayland-server")
wayland_client = pkg_config("wayland-client")
drm = pkg_config("libdrm")
libinput = pkg_config("libinput")
udev = pkg_config("libudev")
xkb = pkg_config("xkbcommon")
seat = pkg_config("libseat")
dbus = pkg_config("dbus-1")
glfw = pkg_config("glfw3")
png = pkg_config("libpng")
vulkan = pkg_config("vulkan")
lunasvg = pkg_config("lunasvg")
sndio = pkg_config("sndio", required=False)
pulse = pkg_config("libpulse", required=False)

system = dependency(ldflags=["-lev", "-lstd", "-lcrypt"])


protocol_root = pkg_config_variable("wayland-protocols", "pkgdatadir")

server_protocol_paths = [
    "stable/xdg-shell/xdg-shell",
    "stable/viewporter/viewporter",
    "stable/linux-dmabuf/linux-dmabuf-v1",
    "stable/presentation-time/presentation-time",
    "staging/color-management/color-management-v1",
    "unstable/xdg-decoration/xdg-decoration-unstable-v1",
    "unstable/primary-selection/primary-selection-unstable-v1",
    "unstable/tablet/tablet-unstable-v2",
    "staging/cursor-shape/cursor-shape-v1",
    "staging/single-pixel-buffer/single-pixel-buffer-v1",
    "staging/xdg-activation/xdg-activation-v1",
    "unstable/xdg-output/xdg-output-unstable-v1",
    "staging/fractional-scale/fractional-scale-v1",
    "unstable/pointer-gestures/pointer-gestures-unstable-v1",
    "unstable/relative-pointer/relative-pointer-unstable-v1",
    "unstable/pointer-constraints/pointer-constraints-unstable-v1",
    "unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1",
    "unstable/idle-inhibit/idle-inhibit-unstable-v1",
    "staging/ext-idle-notify/ext-idle-notify-v1",
    "staging/linux-drm-syncobj/linux-drm-syncobj-v1",
    "staging/xdg-toplevel-icon/xdg-toplevel-icon-v1",
]


def protocol_rule(path, kind, out_dir):
    name = path.rsplit("/", 1)[-1]
    xml = f"{protocol_root}/{path}.xml"
    header = f"$(B)/{out_dir}/{name}-{kind}-protocol.h"
    code_name = f"{name}-protocol.c" if kind == "server" else f"{name}-client-code.c"
    code = f"$(B)/{out_dir}/{code_name}"
    return command(
        name=f"{kind}_{name}",
        inputs=[xml],
        outputs=[header, code],
        descr='WL',
        cmd=[
            ["wayland-scanner", f"{kind}-header", xml, header],
            ["wayland-scanner", "private-code", xml, code],
        ],
    )


server_rules = [protocol_rule(path, "server", "protocols") for path in server_protocol_paths]
server_sources = [
    f"$(B)/protocols/{path.rsplit('/', 1)[-1]}-protocol.c"
    for path in server_protocol_paths
]

protocols = library(
    name="protocols",
    srcs=server_sources,
    deps=[wayland_server],
    cflags=common_cflags,
)


shader_rules = []
for shader in ["cm_convert", "lock_blur"]:
    shader_rules.append(command(
        name=f"shader_{shader}",
        inputs=[f"$(S)/{shader}.comp"],
        outputs=[f"$(B)/shaders/{shader}.spv.h"],
        cmd=[
            "glslangValidator", "-V", f"$(S)/{shader}.comp",
            "--variable-name", f"{shader}_spv", "-o", f"$(B)/shaders/{shader}.spv.h",
        ],
    ))


imgui = library(
    name="imgui",
    srcs=[
        "$(S)/third_party/imgui/imgui.cpp",
        "$(S)/third_party/imgui/imgui_draw.cpp",
        "$(S)/third_party/imgui/imgui_tables.cpp",
        "$(S)/third_party/imgui/imgui_widgets.cpp",
        "$(S)/third_party/imgui/imgui_impl_vulkan.cpp",
        "$(S)/third_party/imgui/imgui_impl_glfw.cpp",
    ],
    cflags=common_cxxflags,
    public_cflags=["-DGLFW_INCLUDE_NONE"],
    deps=[vulkan, glfw],
)


imway_sources = [
    "$(S)/main.cpp", "$(S)/main_composer.cpp", "$(S)/main_supervisor.cpp", "$(S)/main_screenshot.cpp",
    "$(S)/composer.cpp", "$(S)/frame_resource.cpp", "$(S)/pooled.cpp", "$(S)/pooled_ev.cpp", "$(S)/pooled_fd.cpp",
    "$(S)/pooled_vk.cpp", "$(S)/scene.cpp", "$(S)/wayland.cpp", "$(S)/control.cpp", "$(S)/input.cpp",
    "$(S)/intr_list.cpp", "$(S)/listener.cpp", "$(S)/keyboard.cpp", "$(S)/theme.cpp", "$(S)/dialog.cpp",
    "$(S)/desktop_chrome.cpp", "$(S)/dock.cpp", "$(S)/lock_screen.cpp", "$(S)/launcher.cpp", "$(S)/calendar.cpp",
    "$(S)/inspector.cpp", "$(S)/settings.cpp", "$(S)/shadow.cpp", "$(S)/toast.cpp", "$(S)/history.cpp",
    "$(S)/dbus_conn.cpp", "$(S)/notifications.cpp", "$(S)/notifier.cpp", "$(S)/status_notifier.cpp",
    "$(S)/mixer.cpp", "$(S)/mixer_sndio.cpp", "$(S)/mixer_pulse.cpp", "$(S)/osd.cpp", "$(S)/wifi.cpp",
    "$(S)/wifi_iwd.cpp", "$(S)/wifi_nm.cpp", "$(S)/wifi_ui.cpp", "$(S)/icon_store.cpp", "$(S)/icon_pool.cpp",
    "$(S)/icon.cpp", "$(S)/device_vk.cpp", "$(S)/tex_pool.cpp", "$(S)/input_sink.cpp", "$(S)/xdg_utils.cpp",
    "$(S)/session.cpp", "$(S)/device.cpp", "$(S)/device_kms.cpp", "$(S)/device_headless.cpp", "$(S)/util.cpp",
    "$(S)/output.cpp", "$(S)/frame_listener.cpp", "$(S)/renderer.cpp",
]

imway = program(
    name="imway",
    srcs=imway_sources,
    cflags=common_cxxflags,
    ldflags=common_ldflags,
    deps=[
        imgui, protocols,
        wayland_server, wayland_client, drm, libinput, udev, xkb, seat, dbus, glfw,
        png, vulkan, lunasvg, system, sndio, pulse,
    ],
)


client_protocol_paths = [
    "stable/xdg-shell/xdg-shell",
    "stable/viewporter/viewporter",
    "stable/linux-dmabuf/linux-dmabuf-v1",
    "stable/presentation-time/presentation-time",
    "unstable/xdg-output/xdg-output-unstable-v1",
    "unstable/xdg-decoration/xdg-decoration-unstable-v1",
    "unstable/primary-selection/primary-selection-unstable-v1",
    "unstable/relative-pointer/relative-pointer-unstable-v1",
    "unstable/pointer-constraints/pointer-constraints-unstable-v1",
    "unstable/pointer-gestures/pointer-gestures-unstable-v1",
    "unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1",
    "unstable/idle-inhibit/idle-inhibit-unstable-v1",
    "unstable/tablet/tablet-unstable-v2",
    "staging/fractional-scale/fractional-scale-v1",
    "staging/single-pixel-buffer/single-pixel-buffer-v1",
    "staging/xdg-activation/xdg-activation-v1",
    "staging/cursor-shape/cursor-shape-v1",
    "staging/ext-idle-notify/ext-idle-notify-v1",
    "staging/linux-drm-syncobj/linux-drm-syncobj-v1",
    "staging/xdg-toplevel-icon/xdg-toplevel-icon-v1",
    "staging/color-management/color-management-v1",
]

client_rules = [protocol_rule(path, "client", "tests") for path in client_protocol_paths]
client_sources = [
    f"$(B)/tests/{path.rsplit('/', 1)[-1]}-client-code.c"
    for path in client_protocol_paths
]

client_protocols = library(
    name="client_protocols",
    srcs=client_sources,
    deps=[wayland_client],
    cflags=common_cflags,
)


tests = []
for source in sorted(glob.glob("dev/tests/client_*.c") + glob.glob("dev/tests/client_*.cpp")):
    name = os.path.basename(source).rsplit(".", 1)[0]
    flags = common_cxxflags if source.endswith(".cpp") else common_cflags
    tests.append(program(
        name=name,
        output=f"$(B)/tests/{name}",
        srcs=[f"$(S)/{source}"],
        cflags=flags,
        ldflags=common_ldflags,
        deps=[client_protocols, wayland_client, drm, dbus],
    ))


install(imway, *tests)
