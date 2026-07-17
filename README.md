# imway

A Wayland compositor built on Dear ImGui: client windows live inside ImGui
windows (textures via `ImGui::Image`), and all the chrome — menus, panels,
decorations — is drawn by ImGui.

## Stack

Raw, no frameworks (no wlroots/Smithay):

- **libwayland-server** — protocols implemented by hand
- **Vulkan** — the only render path, not a single GL/EGL call
  (headless offscreen + readback; ImGui via `imgui_impl_vulkan`)
- **libdrm** — atomic KMS + dumb buffers for scanout
- **libinput + xkbcommon** — input
- **libev** — event loop

## What already works

- wl_compositor, wl_shm, wl_subcompositor (sync/desync, z-order),
  wl_seat v5 (keyboard + mouse, xkb layouts), wl_output,
  xdg-shell (toplevels with interactive move/resize/fullscreen, popups
  with grabs), xdg-decoration (server-side), wp_viewporter,
  zwp_linux_dmabuf_v1 v3/v4 with feedback (multi-plane dmabuf import into
  VkImage, zero-copy), clipboard + drag-and-drop + primary selection,
  cursor-shape + client cursor surfaces, wp_presentation,
  xdg-activation, single-pixel-buffer
- Backends: `headless` (screenshots, input injection via a `--control` FIFO)
  and `kms` (atomic modeset, scanout swapchain with a dumb-buffer fallback,
  page-flip driven frame clock, libinput, libseat session with VT switching,
  connector hotplug)
- foot, mc and Firefox inside ImGui windows, interactive with mouse/keyboard

## Development

The reproducible Linux development environment is provided by the Nix flake:

```sh
git submodule update --init
nix --extra-experimental-features "nix-command flakes" develop
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

AddressSanitizer and UndefinedBehaviorSanitizer use a separate build tree and
also instrument the vendored libstd:

```sh
cmake -S . -B build-sanitize -G Ninja -DIMWAY_SANITIZERS=ON
cmake --build build-sanitize
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-sanitize --output-on-failure
```

The QEMU workflow (Debian, aarch64+hvf on macOS) remains available:

```sh
git submodule update --init   # vendored libstd (third_party/libstd)
dev/vm/create.sh   # one-time: download the image, cloud-init with the toolchain
dev/build.sh       # rsync sources into the VM + build + ctest
dev/vm/gui.sh      # QEMU window with imway on KMS + foot (mouse/keyboard work)
```

On Linux `dev/build.sh` builds natively. The vendored
[libstd](https://github.com/pg83/std) is built as part of the build; configure
with `-DIMWAY_USE_VENDORED_STD=OFF` to link a system-installed one (`-lstd`,
headers expected on the compiler's default include path). Tests are headless screenshots with
pixel checks: shm, subsurfaces, viewporter, dmabuf (via udmabuf), and a
keyboard e2e test (typing a command into foot).

The target platform is [stal/ix](https://stal-ix.github.io/): fully static
linking, with the Vulkan driver linked into the binary.
