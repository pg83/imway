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
    "third_party/imgui",
    f"{B}/protocols",
    f"{B}/shaders",
    f"{B}/tests",
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
    header = f"{B}/{out_dir}/{name}-{kind}-protocol.h"
    code_name = f"{name}-protocol.c" if kind == "server" else f"{name}-client-code.c"
    code = f"{B}/{out_dir}/{code_name}"
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
    f"{B}/protocols/{path.rsplit('/', 1)[-1]}-protocol.c"
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
        inputs=[f"{shader}.comp"],
        outputs=[f"shaders/{shader}.spv.h"],
        cmd=[
            "glslangValidator", "-V", f"{S}/{shader}.comp",
            "--variable-name", f"{shader}_spv", "-o", f"{B}/shaders/{shader}.spv.h",
        ],
    ))


imgui = library(
    name="imgui",
    srcs=[
        "third_party/imgui/imgui.cpp",
        "third_party/imgui/imgui_draw.cpp",
        "third_party/imgui/imgui_tables.cpp",
        "third_party/imgui/imgui_widgets.cpp",
        "third_party/imgui/imgui_impl_vulkan.cpp",
        "third_party/imgui/imgui_impl_glfw.cpp",
    ],
    cflags=common_cxxflags,
    public_cflags=["-DGLFW_INCLUDE_NONE"],
    deps=[vulkan, glfw],
)


imway_sources = [
    "main.cpp", "main_composer.cpp", "main_supervisor.cpp", "main_screenshot.cpp",
    "composer.cpp", "frame_resource.cpp", "pooled.cpp", "pooled_ev.cpp", "pooled_fd.cpp",
    "pooled_vk.cpp", "scene.cpp", "wayland.cpp", "control.cpp", "input.cpp",
    "intr_list.cpp", "listener.cpp", "keyboard.cpp", "theme.cpp", "dialog.cpp",
    "desktop_chrome.cpp", "dock.cpp", "lock_screen.cpp", "launcher.cpp", "calendar.cpp",
    "inspector.cpp", "settings.cpp", "shadow.cpp", "toast.cpp", "history.cpp",
    "dbus_conn.cpp", "notifications.cpp", "notifier.cpp", "status_notifier.cpp",
    "mixer.cpp", "mixer_sndio.cpp", "mixer_pulse.cpp", "osd.cpp", "wifi.cpp",
    "wifi_iwd.cpp", "wifi_nm.cpp", "wifi_ui.cpp", "icon_store.cpp", "icon_pool.cpp",
    "icon.cpp", "device_vk.cpp", "tex_pool.cpp", "input_sink.cpp", "xdg_utils.cpp",
    "session.cpp", "device.cpp", "device_kms.cpp", "device_headless.cpp", "util.cpp",
    "output.cpp", "frame_listener.cpp", "renderer.cpp",
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
    f"{B}/tests/{path.rsplit('/', 1)[-1]}-client-code.c"
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
        output=f"tests/{name}",
        srcs=[source],
        cflags=flags,
        ldflags=common_ldflags,
        deps=[client_protocols, wayland_client, drm, dbus],
    ))


install(imway, *tests)
