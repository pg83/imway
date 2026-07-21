import build
import os


build.cflags += ["-O2", "-g"]
build.cxxflags += ["-std=c++23"]
build.cppflags += ["-DGLFW_INCLUDE_NONE"]
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
jxl = pkg_config("libjxl")
display_info = pkg_config("libdisplay-info")
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
)


shader_rules = []
for shader in ["cm_convert", "lock_blur", "imgui_scene", "output_transform", "output_transform_vert", "screenshot_pq"]:
    stage = "vert" if shader == "output_transform_vert" else "frag" if shader in ["imgui_scene", "output_transform", "screenshot_pq"] else "comp"
    shader_rules.append(command(
        name=f"shader_{shader}",
        inputs=[f"$(S)/{shader}.{stage}"],
        outputs=[f"$(B)/shaders/{shader}.spv.h"],
        descr='SH',
        cmd=[
            "glslangValidator", "-V", f"$(S)/{shader}.{stage}",
            "--variable-name", f"{shader}_spv", "-o", f"$(B)/shaders/{shader}.spv.h",
        ],
    ))


imgui = library(
    name="imgui",
    srcs=build.glob("$(S)/third_party/imgui/*.cpp"),
    deps=[vulkan, glfw],
)


imway_sources = build.glob("$(S)/*.cpp")

imway = program(
    name="imway",
    srcs=imway_sources,
    deps=[
        imgui, protocols,
        wayland_server, wayland_client, drm, libinput, udev, xkb, seat, dbus, glfw,
        png, jxl, display_info, vulkan, lunasvg, system, sndio, pulse,
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
)


tests = []
for source in sorted(build.glob("$(S)/dev/tests/client_*.c") + build.glob("$(S)/dev/tests/client_*.cpp")):
    name = os.path.basename(source).rsplit(".", 1)[0]
    test_sources = [source]
    test_deps = [client_protocols, wayland_client, drm, dbus]

    if name == "client_reg_screenshot_copy":
        test_deps.append(jxl)

    if name in ("client_reg_color_model", "client_reg_direct_scanout_color",
                "client_reg_display_color", "client_reg_tone_mapping",
                "client_reg_hdr_metadata"):
        test_sources.append("$(S)/color.cpp")
        test_deps.append(display_info)

    tests.append(program(
        name=name,
        output=f"$(B)/tests/{name}",
        srcs=test_sources,
        deps=test_deps,
    ))


install(imway, *tests)
