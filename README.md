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

```sh
git submodule update --init   # vendored libstd (third_party/libstd)
dev/build.sh                  # build-boot/imway + the test clients
dev/test.sh                   # integration tests, a fresh headless compositor per test
```

`dev/build.sh` assumes every dependency is on the compiler's default paths (no
cmake, no pkg-config, no detection). `dev/test.sh` only runs what `dev/build.sh`
built; `IMWAY=path dev/test.sh` points the suite at another binary. Tests are
headless screenshots with pixel checks: shm, subsurfaces, viewporter, dmabuf
(via udmabuf), popups, protocol errors, and a keyboard e2e test (typing a
command into foot).

The target platform is [stal/ix](https://stal-ix.github.io/): fully static
linking, with the Vulkan driver linked into the binary.
