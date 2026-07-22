import build
import build.flags as flags
import fnmatch
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
lcms = pkg_config("lcms2")
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
    "staging/color-representation/color-representation-v1",
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
    "staging/xdg-system-bell/xdg-system-bell-v1",
    "staging/alpha-modifier/alpha-modifier-v1",
    "staging/xdg-dialog/xdg-dialog-v1",
    "staging/content-type/content-type-v1",
    "staging/pointer-warp/pointer-warp-v1",
    "staging/tearing-control/tearing-control-v1",
    "staging/fifo/fifo-v1",
    "staging/commit-timing/commit-timing-v1",
    "staging/ext-image-capture-source/ext-image-capture-source-v1",
    "staging/ext-image-copy-capture/ext-image-copy-capture-v1",
    "staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1",
    "staging/ext-data-control/ext-data-control-v1",
    "unstable/text-input/text-input-unstable-v3",
    "staging/security-context/security-context-v1",
    "stable/tablet/tablet-v2",
    "staging/xdg-toplevel-tag/xdg-toplevel-tag-v1",
    "unstable/xdg-foreign/xdg-foreign-unstable-v2",
    "staging/drm-lease/drm-lease-v1",
    "staging/xdg-toplevel-drag/xdg-toplevel-drag-v1",
]


def protocol_rule(path, kind, out_dir, root=None):
    name = path.rsplit("/", 1)[-1]
    xml = f"{root or protocol_root}/{path}.xml"
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


# protocols the installed wayland-protocols does not carry (wlr extras),
# vendored under dev/protocols
local_protocol_paths = [
    "wlr-screencopy-unstable-v1",
    "input-method-unstable-v2",
    "virtual-keyboard-unstable-v1",
]

server_rules = [protocol_rule(path, "server", "protocols") for path in server_protocol_paths] + [
    protocol_rule(path, "server", "protocols", root="$(S)/dev/protocols") for path in local_protocol_paths
]
server_sources = [
    f"$(B)/protocols/{path.rsplit('/', 1)[-1]}-protocol.c"
    for path in server_protocol_paths + local_protocol_paths
]

protocols = library(
    name="protocols",
    srcs=server_sources,
    deps=[wayland_server],
)


# shaders are named after the .cpp that creates their pipeline;
# fullscreen.vert is the shared fullscreen-triangle vertex stage
shader_rules = []
for shader, stage in [
    ("fullscreen", "vert"),
    ("renderer_scene", "frag"),
    ("renderer_output", "frag"),
    ("renderer_cursor", "frag"),
    ("main_screenshot_scene", "frag"),
    ("main_screenshot_output", "frag"),
    ("lock_screen_blur", "comp"),
]:
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
        png, jxl, lcms, display_info, vulkan, lunasvg, system, sndio, pulse,
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
    "staging/color-representation/color-representation-v1",
    "staging/xdg-system-bell/xdg-system-bell-v1",
    "staging/alpha-modifier/alpha-modifier-v1",
    "staging/xdg-dialog/xdg-dialog-v1",
    "staging/content-type/content-type-v1",
    "staging/pointer-warp/pointer-warp-v1",
    "staging/tearing-control/tearing-control-v1",
    "staging/fifo/fifo-v1",
    "staging/commit-timing/commit-timing-v1",
    "staging/ext-image-capture-source/ext-image-capture-source-v1",
    "staging/ext-image-copy-capture/ext-image-copy-capture-v1",
    "staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1",
    "staging/ext-data-control/ext-data-control-v1",
    "unstable/text-input/text-input-unstable-v3",
    "staging/security-context/security-context-v1",
    "stable/tablet/tablet-v2",
    "staging/xdg-toplevel-tag/xdg-toplevel-tag-v1",
    "unstable/xdg-foreign/xdg-foreign-unstable-v2",
    "staging/xdg-toplevel-drag/xdg-toplevel-drag-v1",
]

client_rules = [protocol_rule(path, "client", "tests") for path in client_protocol_paths] + [
    protocol_rule(path, "client", "tests", root="$(S)/dev/protocols") for path in local_protocol_paths
]
client_sources = [
    f"$(B)/tests/{path.rsplit('/', 1)[-1]}-client-code.c"
    for path in client_protocol_paths + local_protocol_paths
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
        test_deps += [jxl, png]

    if name in ("client_reg_color_icc", "client_reg_color_icc_validation"):
        test_deps.append(lcms)

    if name in ("client_reg_color_model", "client_reg_direct_scanout_color",
                "client_reg_display_color", "client_reg_tone_mapping",
                "client_reg_hdr_metadata"):
        test_sources.append("$(S)/color.cpp")
        test_deps.append(display_info)

    if name == "client_reg_scanout_opaque_policy":
        test_sources += ["$(S)/scene.cpp", "$(S)/color.cpp"]
        test_deps.append(display_info)

    tests.append(program(
        name=name,
        output=f"$(B)/tests/{name}",
        srcs=test_sources,
        deps=test_deps,
    ))


install(imway, *tests)


# ---- integration tests as graph nodes -------------------------------------
# Each scenario becomes `runs` command nodes (default 3, -Druns=N): a node
# runs one headless compositor + scenario and writes a JSON verdict, always
# exiting 0 so a failure does not abort the graph. One final `test` node
# depends on every per-run node, reads all the JSONs and produces the verdict
# that fails `./build test`. -Dfilter=GLOB restricts which scenarios build.
runs = int(flags.runs) if flags.runs else 3
test_filter = flags.filter

scenarios = sorted(build.glob("$(S)/dev/tests/headless_*.sh"))
# every non-scenario file a scenario may source (lib.sh, *_case.sh, *.inc),
# plus the runner itself: any change to the harness re-runs every test
harness = sorted(
    set(build.glob("$(S)/dev/tests/*.sh")) - set(scenarios)
) + sorted(build.glob("$(S)/dev/tests/*.inc")) + ["$(S)/dev/run_test.py"]

client_by_name = {target.name: target for target in tests}

test_nodes = []
for scenario in scenarios:
    name = os.path.basename(scenario)[:-len(".sh")]
    if test_filter and not fnmatch.fnmatch(name, test_filter):
        continue

    client_name = name.replace("headless_", "client_", 1)
    client_target = client_by_name.get(client_name)
    # every client binary is a dependency: the harness (lib.sh, *_case.sh,
    # matrix_run.sh) resolves shared helpers — the input-health probe, client_ok,
    # the render-matrix client — by path next to the scenario's own client, so
    # the whole tests/ dir must be restored, not just this test's client
    node_deps = [imway, *tests]

    for run_index in range(runs):
        out = f"$(B)/test-results/{name}.run{run_index}.json"
        cmd = [
            "python3", "$(S)/dev/run_test.py",
            "--scenario", scenario,
            "--imway", "$(B)/imway",
            "--out", out,
            "--run", str(run_index),
        ]
        if client_target:
            cmd += ["--client", f"$(B)/tests/{client_name}"]
        test_nodes.append(command(
            name=f"test_{name}_r{run_index}",
            inputs=[scenario, *harness],
            outputs=[out],
            deps=node_deps,
            cmd=cmd,
            descr="TEST",
            color="cyan",
        ))

if test_nodes:
    verdict_cmd = [
        "python3", "$(S)/dev/aggregate_tests.py",
        "--results", "$(B)/test-results",
        "--out", "$(B)/test-results/verdict.txt",
    ]
    if flags.allow_flaky:
        verdict_cmd.append("--allow-flaky")
    test = command(
        name="test",
        inputs=["$(S)/dev/aggregate_tests.py"],
        outputs=["$(B)/test-results/verdict.txt"],
        deps=test_nodes,
        cmd=verdict_cmd,
        descr="VERDICT",
        color="light-green",
    )
